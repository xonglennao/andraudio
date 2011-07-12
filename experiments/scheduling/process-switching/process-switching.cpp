/* This program is placed in the public domain.
 * AUTHORS:
 *    Gabriel M. Beddingfield <gabrbedd@gmail.com>
 */

/* process-switching.cpp
 * 2011-07-08
 *
 * Application to test the speed in switching between
 * N process controlled by a master process (main).
 *
 * Build instructions:
 *   $ g++ -o process-switching process-switching.cpp -lpthread -lrt
 *   $ ./thread-switching
 *
 * Test methodology:
 *   - Master process (no args) spawns several child
 *     processes (same program with hook to shm).
 *   - The threads signal run/return using POSIX
 *     semaphores (sem_t).
 *   - Each thread simply grabs the current time
 *     and puts a message on the logger queue
 *     (Details::Message, Details::Logger).
 *   - At the end, the logger queue is emptied
 *     and statistics are calculated.
 */

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <sys/time.h>
#include <vector>
#include <iostream>
#include <cassert>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>

using namespace std;

#define SHM_NAME "/process-switching"
#define RUN_INTERVAL 40000

namespace Details
{
    struct Message
    {
        uint8_t thread;
        timeval time;
    };

    struct Sync
    {
        sem_t run;
        sem_t ret;
        volatile bool kill;
        Message m;
    };

    double usec_avg_sum;
    unsigned long usec_avg_count;

#ifdef USE_LOGGER
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
            assert(m_write > 0);
            if(unsigned(m_write) >= m_queue.size()) {
                m_write = 0;
            }
        }

        void m_inc_read() {
            // PRECONDITION: m_mutex must be locked
            ++m_read;
            assert(m_read > 0);
            if(unsigned(m_read) >= m_queue.size()) {
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
#endif // USE_LOGGER

    /* If b is after a, then value will be > 0
     */
    static double calc_dt_double(const timeval& a, const timeval& b) {
        return (double(b.tv_sec) - double(a.tv_sec)) * 1000000.0
            + double(b.tv_usec) - double(a.tv_usec);
    }

    static unsigned long calc_dt_unsigned(const timeval& a, const timeval& b) {
        unsigned long ans = 0;
        assert(a.tv_usec < 1000000);
        assert(b.tv_usec < 1000000);
        if( b.tv_sec > a.tv_sec ) {
            ans = (b.tv_sec - a.tv_sec) * 1000000 + b.tv_usec - a.tv_usec;
        } else if (b.tv_sec == a.tv_sec) {
            if(b.tv_usec > a.tv_usec) {
                ans = b.tv_usec - a.tv_usec;
            }
        }
        return ans;
    }

    static long calc_dt_int(const timeval& a, const timeval& b) {
        long ans;
        assert(a.tv_usec < 1000000);
        assert(b.tv_usec < 1000000);
        ans = (b.tv_sec - a.tv_sec) * 1000000 + b.tv_usec - a.tv_usec;
        return ans;
    }

    class Stats
    {
    public:
        Stats() { reset(); }
        ~Stats() {}

        void new_cycle(const timeval& expected) {
            timeval now;
            double dt;
            gettimeofday(&now, 0);
            ++N_cycles;
            dt = calc_dt_double(expected, now);
            sum_cycle_dt += dt;
            if(dt > max_cycle_dt)
                max_cycle_dt = dt;
            if(dt < min_cycle_dt)
                min_cycle_dt = dt;
            m_last_time = now;
        }

        void update(const Message m) {
            double dt;
            ++N_dt;
            dt = calc_dt_double(m_last_time, m.time);
            assert(dt >= 0.0);
            sum_dt += dt;
            if(dt > max_dt)
                max_dt = dt;
            if(dt < min_dt)
                min_dt = dt;
        }

        void report() {
            double cycle_accuracy = sum_cycle_dt / N_cycles;
            double switch_speed = sum_dt / N_dt;
            cout << "Cycle scheduling happened (avg) within "
                 << cycle_accuracy << " usecs of the appointed time."
                 << endl
                 << "max/min = " << max_cycle_dt << "/" << min_cycle_dt
                 << endl;
            cout << "Thread switching speed was avg "
                 << switch_speed << " usecs"
                 << endl
                 << "max/min = " << max_dt << "/" << min_dt
                 << endl;
        }

        void reset() {
            N_cycles = 0;
            sum_cycle_dt = 0.0;
            max_cycle_dt = 0.0;
            min_cycle_dt = 0.0;
            m_last_time.tv_sec = 0; m_last_time.tv_usec = 0;
            N_dt = 0;
            sum_dt = 0.0;
            max_dt = 0.0;
            min_dt = 0.0;
        }

    private:
        unsigned long N_cycles;
        double sum_cycle_dt;
        double max_cycle_dt;
        double min_cycle_dt;
        timeval m_last_time;
        unsigned long N_dt;
        double sum_dt;
        double max_dt;
        double min_dt;

    }; // class Stats

    static void output_message(const Message& m, const timeval& t_zero)
    {
        unsigned long usecs, dt;
        static unsigned long last = 0;
        usecs = (m.time.tv_sec - t_zero.tv_sec) + m.time.tv_usec - t_zero.tv_usec;
        dt = usecs - last;
        ++usec_avg_count;
        usec_avg_sum += dt;
        cout << "proc=" << int(m.thread)
             << " time=" << usecs
             << " dt=" << dt << endl;
        last = usecs;
    }

} // namespace Details

int main(int argc, char* argv[])
{
    const int NPROCS = 32;
    const int NCYCLES = (1L << 16);
    const unsigned MAXMSG = 4096;
#ifdef USE_LOGGER
    Details::Logger log(MAXMSG+1);
#endif
    Details::Sync *proc_sync;
    Details::Sync *client_sync;
    pid_t procs[NPROCS];
    void *shm_mem;
    int k,j;

#ifdef USE_LOGGER
    assert( NPROCS*NCYCLES < MAXMSG );
#endif

    cout << "Running with:" << endl
         << NPROCS << " processes" << endl
         << NCYCLES << " cycles" << endl
         << RUN_INTERVAL << " usec/cycle" << endl;
    cout << "This test should take about " << (double(RUN_INTERVAL)*double(NCYCLES)/1000000.0) << " secs" << endl;

    // Prepares shared memory
    shm_mem = mmap(0, NPROCS * sizeof(Details::Sync), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    assert(shm_mem != MAP_FAILED);
    proc_sync = static_cast<Details::Sync*>(shm_mem);
    client_sync = proc_sync;

    // Prepare semaphores and such.
    for(k=0 ; k<NPROCS ; ++k) {
        sem_init( &(proc_sync[k].run), 1, 0 );
        sem_init( &(proc_sync[k].ret), 1, 0 );
        proc_sync[k].m.thread = k;
        proc_sync[k].kill = false;
    }

    // Spawn other processes
    for(k=0 ; k<NPROCS ; ++k) {
        procs[k] = fork();
        if(procs[k] == 0) {
            Details::Sync& s = proc_sync[k];
            // Client loop
            while(0 == sem_wait(&s.run)) {
                if(s.kill)
                    exit(0);
                gettimeofday(&s.m.time, 0);
                sem_post(&s.ret);
                if(s.kill)
                    exit(0);
            }
        }
    }

    unsigned long interval;
    long balance; //usecs
    timeval t_zero, t_next, t_now;
    Details::Stats stats;

    interval = RUN_INTERVAL;
    Details::usec_avg_sum = 0.0;
    Details::usec_avg_count = 0;
    gettimeofday(&t_zero, 0);
    t_next = t_zero;
    for(j=0 ; j<NCYCLES ; ++j) {
        stats.new_cycle(t_next);
        for(k=0 ; k<NPROCS ; ++k) {
            sem_post(&proc_sync[k].run);
            sem_wait(&proc_sync[k].ret);
#ifdef USE_LOGGER
            log.push(proc_sync[k].m);
#endif
            stats.update(proc_sync[k].m);
        }
        t_next.tv_usec += interval;
        if(t_next.tv_usec > 100000) {
            t_next.tv_sec += 1;
            t_next.tv_usec -= 1000000;
        }
        gettimeofday(&t_now, 0);
        balance = Details::calc_dt_int(t_now, t_next);
        if(balance > 100) {
            //cerr << "sleeping " << balance << endl;
            usleep(balance);
        }
    }

    for(k=0 ; k<NPROCS ; ++k) {
        proc_sync[k].kill = true;
        sem_post(&proc_sync[k].run);
    }

    sleep(1);

#ifdef USE_LOGGER
    while( log.pop(m) ) {
        output_message(m, t_zero);
    }
#endif
    stats.report();

#if 0
    cout << "Average delay was "
         << (double(Details::usec_avg_sum)/double(Details::usec_avg_count))
         << " micro-seconds"
         << endl;
#endif
    cout << "Exited cleanly" << endl;

    return 0;
}

