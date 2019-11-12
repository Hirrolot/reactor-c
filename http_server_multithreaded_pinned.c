/*
 * Версия многопоточного сервера, где потоки прибиты к ядрам. Производительность
 * почти такая же, как и у http_server_multithreaded.c:
 *
 * $ wrk -c100 -d1m -t8 http://127.0.0.1:18470 -H "Host: 127.0.0.1:18470" \
 *      -H "Accept-Language: en-US,en;q=0.5" -H "Connection: keep-alive"
 * Running 1m test @ http://127.0.0.1:18470
 *  8 threads and 100 connections
 *  Thread Stats   Avg      Stdev     Max   +/- Stdev
 *    Latency   495.55us    1.00ms  25.84ms   89.62%
 *    Req/Sec    79.39k    31.63k  159.24k    66.97%
 *  37914962 requests in 1.00m, 5.19GB read
 * Requests/sec: 631227.47
 * Transfer/sec:     88.49MB
 *
 * ------------------------------
 *
 * $ sudo perf stat ./http_server_multithreaded_pinned
 * Performance counter stats for './http_server_multithreaded_pinned':
 *
 *     241364,468945      task-clock (msec)         #    3,996 CPUs utilized
 *         2 015 452      context-switches          #    0,008 M/sec
 *                12      cpu-migrations            #    0,000 K/sec
 *               244      page-faults               #    0,001 K/sec
 *   886 526 857 409      cycles                    #    3,673 GHz
 *   617 732 545 050      instructions              #    0,70  insn per cycle
 *   119 311 277 122      branches                  #  494,320 M/sec
 *     3 285 806 736      branch-misses             #    2,75% of all branches
 *
 *      60,396719472 seconds time elapsed
 */

#include <stdbool.h>

#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>

#include "reactor.h"

static _Thread_local Reactor *reactor;

#include "common.h"

#define LOGICAL_CORES 8

//************************************************************

void *start_thread(void *arg) {
    int cpu_id = *(int *)arg;
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(cpu_id, &cpu_set);

    SAFE_CALL(sched_setaffinity(0, sizeof(cpu_set), &cpu_set) == -1, true);

    SAFE_CALL((reactor = reactor_new()), NULL);
    SAFE_CALL(
        reactor_register(reactor, new_server(true), EPOLLIN, on_accept, NULL),
        -1);
    SAFE_CALL(reactor_run(reactor, SERVER_TIMEOUT_MILLIS), -1);
    SAFE_CALL(reactor_destroy(reactor), -1);

    return NULL;
}

//************************************************************

int main(void) {
    pthread_t threads[LOGICAL_CORES];

    for (int i = 0; i < LOGICAL_CORES; i++) {
        SAFE_CALL(pthread_create(&threads[i], NULL, start_thread, &(int){i}) ==
                      0,
                  false);
    }

    for (int i = 0; i < LOGICAL_CORES; i++) {
        SAFE_CALL(pthread_join(threads[i], NULL) == 0, false);
    }
}