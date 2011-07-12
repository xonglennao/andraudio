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
 *   - Each process simply grabs the current time
 *     and returns it to the master process.
 *   - The master process tracks statistics about
 *     runtime.
 */

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <sys/time.h>
#include <iostream>
#include <cassert>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>

using namespace std;

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
        unsigned long N;
        double sum_x;
        double sum_x2;
        double max;
        double min;

        Stats() { reset(); }

        void reset() {
            N = 0;
            sum_x = sum_x2 = max = min = 0.0;
        }

        void insert(double x) {
            ++N;
            sum_x += x;
            sum_x2 += x*x;
            if(x > max) max = x;
            if(x < min) min = x;
            if(N == 1) {
                max = min = x;
            }
        }

        double average() {
            if(N == 0) return 0;
            return sum_x / double(N);
        }

        double stddev() {
            if(N == 0) return 0;
            double avg = average();
            return ::sqrt( sum_x2/double(N) - avg*avg );
        }

    }; // class Stats

    class RunStats
    {
    public:
        RunStats() { reset(); }
        ~RunStats() {}

        void new_cycle(const timeval& expected) {
            timeval now;
            double dt;
            gettimeofday(&now, 0);
            dt = calc_dt_double(expected, now);
            m_cycle.insert(dt);
            m_last_time = now;
        }

        void update(const Message m) {
            double dt;
            dt = calc_dt_double(m_last_time, m.time);
            assert(dt >= 0.0);
            m_thread.insert(dt);
        }

        void report() {
            cerr << "# nproc interval ncycles secs cyc_avg cyc_stddev cyc_max cyc_min "
                 << "thd_avg thd_stddev thd_max thd_min" << endl;
            cerr << m_nprocs
                 << " " << m_interval
                 << " " << m_n_cycles
                 << " " << m_secs
                 << " " << m_cycle.average()
                 << " " << m_cycle.stddev()
                 << " " << m_cycle.max
                 << " " << m_cycle.min
                 << " " << m_thread.average()
                 << " " << m_thread.stddev()
                 << " " << m_thread.max
                 << " " << m_thread.min
                 << endl;
            cout << "Cycle scheduling:"
                 << " avg=" << m_cycle.average()
                 << " stddev=" << m_cycle.stddev()
                 << " max=" << m_cycle.max
                 << " min=" << m_cycle.min
                 << " (usecs)"
                 << endl;
            cout << "Thread switching:"
                 << " avg=" << m_thread.average()
                 << " stddev=" << m_thread.stddev()
                 << " max=" << m_thread.max
                 << " min=" << m_thread.min
                 << " (usecs)"
                 << endl;
        }

        void reset() {
            m_cycle.reset();
            m_thread.reset();
            m_last_time.tv_sec = 0; m_last_time.tv_usec = 0;
        }

        void set_meta(int nprocs, unsigned long interval, unsigned long ncycles, double secs) {
            m_nprocs = nprocs;
            m_interval = interval;
            m_n_cycles = ncycles;
            m_secs = secs;
        }

    private:
        Stats m_cycle;
        Stats m_thread;
        timeval m_last_time;
        // Meta:
        int m_nprocs;
        unsigned long m_interval;
        unsigned long m_n_cycles;
        double m_secs;
    }; // class RunStats

} // namespace Details

int main(int argc, char* argv[])
{
    int NPROCS = 32;
    int NCYCLES;
    int DURATION = 30;
    long RUN_INTERVAL = 40000;
    Details::Sync *proc_sync;
    Details::Sync *client_sync;
    pid_t procs[NPROCS];
    void *shm_mem;
    int k,j;

    if(argc != 4) {
        assert(argc > 0);
        cout << "Usage: " << argv[0] << " <num_processes> <cycle_interval_usecs> <test_duration_secs>" << endl;
        exit(0);
    }

    NPROCS = atoi(argv[1]);
    RUN_INTERVAL = atol(argv[2]);
    DURATION = atoi(argv[3]);
    NCYCLES = double(DURATION) * 1000000.0 / double(RUN_INTERVAL) + 0.5;

    cout << argv[0] << " " << argv[1] << " " << argv[2] << " "
         << argv[3] << " =================================" << endl;
    cout << "Running with: "
         << NPROCS << " processes, "
         << NCYCLES << " cycles, "
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
    Details::RunStats stats;

    interval = RUN_INTERVAL;
    gettimeofday(&t_zero, 0);
    t_next = t_zero;
    stats.set_meta(NPROCS, RUN_INTERVAL, NCYCLES, DURATION);
    for(j=0 ; j<NCYCLES ; ++j) {
        stats.new_cycle(t_next);
        for(k=0 ; k<NPROCS ; ++k) {
            sem_post(&proc_sync[k].run);
            sem_wait(&proc_sync[k].ret);
            stats.update(proc_sync[k].m);
        }
        t_next.tv_usec += interval;
        if(t_next.tv_usec > 100000) {
            t_next.tv_sec += 1;
            t_next.tv_usec -= 1000000;
        }
        gettimeofday(&t_now, 0);
        balance = Details::calc_dt_int(t_now, t_next) - 120L;
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

    stats.report();

    cout << "Exited cleanly" << endl;

    return 0;
}

