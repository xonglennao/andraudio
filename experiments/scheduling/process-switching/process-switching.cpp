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
    const int NCYCLES = 64;
    const unsigned MAXMSG = 4096;
    Details::Logger log(MAXMSG+1);
    Details::Sync *proc_sync;
    Details::Sync *client_sync;
    pid_t procs[NPROCS];
    void *shm_mem;
    timeval t_zero;
    int k,j;

    assert( NPROCS*NCYCLES < MAXMSG );

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

    Details::Message m;

    Details::usec_avg_sum = 0.0;
    Details::usec_avg_count = 0;
    gettimeofday(&t_zero, 0);
    for(j=0 ; j<NCYCLES ; ++j) {
        for(k=0 ; k<NPROCS ; ++k) {
            sem_post(&proc_sync[k].run);
            sem_wait(&proc_sync[k].ret);
            log.push(proc_sync[k].m);
        }
    }

    for(k=0 ; k<NPROCS ; ++k) {
        proc_sync[k].kill = true;
        sem_post(&proc_sync[k].run);
    }

    sleep(1);

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

