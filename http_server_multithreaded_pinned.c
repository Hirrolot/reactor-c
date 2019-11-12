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
}