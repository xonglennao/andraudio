/* This program is placed in the public domain.
 * AUTHORS:
 *    Gabriel M. Beddingfield <gabrbedd@gmail.com>
 */

/* thread-switching.cpp
 * 2011-07-08
 *
 * Application to test the speed in switching between
 * N threads controlled by a master thread (main).
 */

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <sys/time.h>
#include <vector>
#include <iostream>
#include <cassert>

using namespace std;

namespace Details
{
    struct Message
    {
	uint8_t thread;
	timeval time;
    };

    double usec_avg_sum;
    unsigned long usec_avg_count;

    /* A circular buffer for the threads to log their messages
     */
    class Logger
    {
    public:
	Logger(int size = 1024) :
	    m_read(0),
	    m_write(0),
	    m_queue(size) {
	    pthread_mutex_init(&m_mutex, 0);
	}

	int write_space() {
	    int r, w, s;
	    w = m_write;
	    r = m_read;
	    s = m_queue.size();

	    return m_write_space(r, w, s);
	}

	bool push(const Message& m) {
	    int r, w, s;
	    bool rv = false;
	    pthread_mutex_lock(&m_mutex);
	    try {
		r = m_read;
		w = m_write;
		s = m_queue.size();
		if(m_write_space(r, w, s)) {
		    m_queue[w] = m;
		    m_inc_write();
		    rv = true;
		}
	    } catch (...) {
		cerr << "Exception in " << __PRETTY_FUNCTION__ << endl;
	    }
	    pthread_mutex_unlock(&m_mutex);
	    return rv;
	}

	bool pop(Message& m) {
	    int r, w, s;
	    bool rv = false;
	    pthread_mutex_lock(&m_mutex);
	    try {
		r = m_read;
		w = m_write;
		s = m_queue.size();
		if(m_read_space(r, w, s)) {
		    m = m_queue[r];
		    m_inc_read();
		    rv = true;
		}
	    } catch (...) {
		cerr << "Exception in " << __PRETTY_FUNCTION__ << endl;
	    }
	    pthread_mutex_unlock(&m_mutex);
	    return rv;
	}
	
    private:
	void m_inc_write() {
	    // PRECONDITION: m_mutex must be locked
	    ++m_write;
	    if(m_write >= m_queue.size()) {
		m_write = 0;
	    }
	}

	void m_inc_read() {
	    // PRECONDITION: m_mutex must be locked
	    ++m_read;
	    if(m_read >= m_queue.size()) {
		m_read = 0;
	    }
	}

	static int m_write_space(int read, int write, int size) {
	    if(write == read) {
		return size - 1;
	    } else if (write > read) {
		return size - 1 - (write - read);
	    } else {
		return (read - write);
	    }
	}

	static int m_read_space(int read, int write, int size) {
	    if(write == read) {
		return 0;
	    } else if (write > read) {
		return (write - read);
	    } else {
		return size - 1 - (read - write);
	    }
	}

    private:
	pthread_mutex_t m_mutex;
	int m_read, m_write;
	std::vector<Message> m_queue;
    }; // class Logger

    class Thread
    {
    public:
	Thread() : m_logger(0), m_id(-1) {
	    sem_init(&m_return, 0, 0);
	}
	~Thread() {}

	/* Returns a wait condition to signal that we're done with the
	 * current run cycle
	 */
	sem_t* start(Logger *log, uint8_t id, sem_t *sync) {
	    m_logger = log;
	    m_id = id;
	    m_sync = sync;
	    pthread_create(&m_thread, 0, Thread::static_main, this);
	    return &m_return;
	}

	void cancel() {
	    pthread_cancel(m_thread);
	}

	void join() {
	    pthread_join(m_thread, 0);
	}

    private:
	static void* static_main(void* arg) {
	    Thread *that = static_cast<Thread*>(arg);
	    return that->main();
	}

	void* main() {
	    Message m;
	    m.thread = m_id;
	    while(0 == sem_wait(m_sync)) {
		gettimeofday(&m.time, 0);
		m_logger->push(m);
		sem_post(&m_return);
	    }
	    return 0;
	}

    private:
	Logger *m_logger;
	int8_t m_id;
	sem_t *m_sync;
	sem_t m_return;
	pthread_t m_thread;
    };

    static void output_message(const Message& m, const timeval& t_zero)
    {
	unsigned long usecs, dt;
	static unsigned long last = 0;
	usecs = (m.time.tv_sec - t_zero.tv_sec) + m.time.tv_usec - t_zero.tv_usec;
	dt = usecs - last;
	++usec_avg_count;
	usec_avg_sum += dt;
	cout << "thread=" << int(m.thread)
	     << " time=" << usecs
	     << " dt=" << dt << endl;
	last = usecs;
    }

} // namespace Details

int main(int argc, char* argv[])
{
    const int NTHREADS = 32;
    const int NCYCLES = 64;
    const unsigned MAXMSG = 4096;
    Details::Logger log(MAXMSG+1);
    Details::Thread threads[NTHREADS];
    sem_t* returns[NTHREADS];
    sem_t  runs[NTHREADS];
    timeval t_zero;
    int k,j;

    assert( NTHREADS*NCYCLES < MAXMSG );

    for(k=0 ; k<NTHREADS ; ++k) {
	sem_init(&runs[k], 0, 0);
	returns[k] = threads[k].start(&log, k, &runs[k]);
    }

    Details::Message m;

    Details::usec_avg_sum = 0.0;
    Details::usec_avg_count = 0;
    gettimeofday(&t_zero, 0);
    for(j=0 ; j<NCYCLES ; ++j) {
	for(k=0 ; k<NTHREADS ; ++k) {
	    sem_post(&runs[k]);
	    sem_wait(returns[k]);
	}
    }

    for(k=0 ; k<NTHREADS ; ++k) {
	threads[k].cancel();
    }
    for(k=0 ; k<NTHREADS ; ++k) {
	threads[k].join();
    }

    while( log.pop(m) ) {
	output_message(m, t_zero);
    }

    cout << "Average delay was "
	 << (double(Details::usec_avg_sum)/double(Details::usec_avg_count))
	 << " micro-seconds"
	 << endl;
    cout << "Exited cleanly" << endl;

    return 0;
}
