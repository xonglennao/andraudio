/*
 * Copyright (c) 2011 Samalyse
 * Author: Olivier Guilyardi
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Host client inter-thread communication benchmark using semaphores
 *
 * This little test attempts to evaluate the delay/latency induced
 * by the roundtrip inter-thread communications between a host and 
 * its client (plugin).
 * 
 * It uses somehow realistic defaults for an audio cycle time as well
 * as a little work being done in both the host and the client.
 *
 * The host and client threads can be both made to run in realtime
 * by changing the value of REALTIME_PRIORITY.
 */

#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <sched.h>

/* The time this test should run, in seconds */
#define RUNTIME            60 

/* The host main loop cycle period, in microseconds */
#define CYCLE_PERIOD     2000 

/* Some work to do in the host, in microseconds */
#define HOST_WORKTIME     200 

/* Some work to do in the client, in microseconds */
#define CLIENT_WORKTIME   200 

/* The threshold above which the delay is considered critical 
   (includes inter-thread communication and the client work time) */
#define CRITICAL_DELAY    500 

/* Use realtime scheduling if greater than zero (1 - 99) */
#define REALTIME_PRIORITY   0 

static double delay_sum            = 0;
static double delay_max            = 0;
static long   delay_count          = 0;
static long   critical_delay_count = 0;
static double xrun_min             = 0;
static double xrun_max             = 0;
static long   xrun_count           = 0;
static int    progress             = 0;
static int    done                 = 0;
static sem_t call_signal, return_signal, start_signal;

static double get_time() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    double d = t.tv_sec * 1000000.0 + t.tv_nsec / 1000.0;
    return d;
}

static void work(useconds_t duration) {
    double start_time = get_time();
    double value = 17;
    int i;
    while ((get_time() - start_time) < duration) {
        for (i = 0; i < 10; i++)
            value = sqrt(value) * sqrt(value);
    }
}

static void setup_thread(const char *name, int rt_priority) {
    struct sched_param param;
    printf("[%s thread started]", name);
    if (rt_priority) {
        param.sched_priority = rt_priority;
        if (!sched_setscheduler(0, SCHED_FIFO, &param)) {
            printf(" [RT]");
        } else {
            perror("Failed to set realtime priority");
            exit(1);
        }
    }
    printf("\n");
}

static void *host(void *arg) {
    double delay, send_time, recv_time, work_time, xrun;
    double start_time = get_time();
    double timeout    = RUNTIME * 1000000.0;

    setup_thread("Host", REALTIME_PRIORITY);

    while (1) {
        send_time = get_time();

        if (send_time - start_time >= timeout) 
            done = 1;

        if (sem_post(&call_signal)) {
            perror("sem_post() failed in host");
            exit(1);
        }

        if (done) 
            break;

        if (sem_wait(&return_signal)) {
            perror("sem_wait() failed in host");
            exit(1);
        }

        recv_time = get_time();

        delay       = recv_time - send_time;
        delay_sum  += delay;
        delay_count++;

        if (delay > delay_max)
            delay_max = delay;

        if (delay >= CRITICAL_DELAY)
            critical_delay_count++;

        work(HOST_WORKTIME);

        progress = recv_time / 1000000.0;

        work_time = (get_time() - send_time);
        xrun      = work_time - CYCLE_PERIOD;
        if (xrun > 0) {
            // printf("Xrun of %.2f microseconds\n", xrun);
            if (!xrun_min || xrun < xrun_min) 
                xrun_min = xrun;
                
            if (xrun > xrun_max)
                xrun_max = xrun;

            xrun_count++;
        } else {
            usleep(CYCLE_PERIOD - work_time);
        }

    }

    return NULL;
}

static void *client(void *arg) {
    setup_thread("Client", REALTIME_PRIORITY);

    sem_post(&start_signal);

    while (1) {
        if (sem_wait(&call_signal)) {
            perror("sem_wait() failed in client");
            exit(1);
        }

        if (done)
            break;

        work(CLIENT_WORKTIME);     

        if (sem_post(&return_signal)) {
            perror("sem_post() failed in client");
            exit(1);
        }
    }

    return NULL;
}

int main() {
    pthread_t host_thread, client_thread;

    printf("Starting host-client roundtrip test - Runtime: %d seconds\n", RUNTIME);
    double start_time = get_time();

    sem_init(&start_signal, 0, 0);
    sem_init(&call_signal, 0, 0);
    sem_init(&return_signal, 0, 0);

    if (pthread_create(&client_thread, NULL, client, NULL)) {
        printf("Failed to create client thread\n");
        exit(1);
    }
    
    sem_wait(&start_signal);

    if (pthread_create(&host_thread, NULL, host, NULL)) {
        printf("Failed to create host thread\n");
        exit(1);
    }

    int last_progress = 0;
    while (!done) {
        usleep(500000);
        if (last_progress != progress) {
            printf(".");
            fflush(stdout);
            last_progress = progress;
        }
    }

    if (pthread_join(client_thread, NULL)) 
        printf("Warning: failed to join client thread\n");

    if (pthread_join(host_thread, NULL)) 
        printf("Warning: failed to join host thread\n");

    sem_destroy(&return_signal);
    sem_destroy(&call_signal);
    sem_destroy(&start_signal);

    printf("\n==== Parameters ====\n" 
           "Cycle period:               %8.2f microseconds\n"
           "Host work time:             %8.2f microseconds\n"
           "Client work time:           %8.2f microseconds\n"
           "Critical delay threshold:   %8.2f microseconds\n"
           "Realtime priority:          %8.2f\n"

           "==== Results ====\n"
           "Runtime:                    %8.2f seconds\n"
           "Iterations:                 %8.2f calls\n"
           "Average roundtrip delay:    %8.2f microseconds\n"
           "Maximum roundtrip delay:    %8.2f microseconds\n"
           "Critical threshold reached: %8.2f time(s)\n"
           "Number of xruns:            %8.2f\n"
           "Xrun range:                 %8.2f - %.2f microseconds\n", 

           (float) CYCLE_PERIOD,
           (float) HOST_WORKTIME,
           (float) CLIENT_WORKTIME,
           (float) CRITICAL_DELAY,
           (float) REALTIME_PRIORITY,
           (get_time() - start_time) / 1000000.0,
           (float) delay_count,
           delay_sum / delay_count,
           delay_max, 
           (float) critical_delay_count,
           (float) xrun_count,
           xrun_min, xrun_max);

    return 0;
}
