#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <glib.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "reactor.h"

#define MAX_EVENTS 4096

struct reactor {
    int epoll_fd;
    GHashTable *table;
};

static int *int_in_heap(int key) {
    int *result;
    if ((result = malloc(sizeof(*result))) == NULL)
        abort();
    *result = key;
    return result;
}

typedef struct {
    Callback callback;
    void *arg;
} CallbackData;

static CallbackData *callback_data_new(Callback callback, void *arg) {
    CallbackData *result;
    if ((result = malloc(sizeof(*result))) == NULL)
        abort();

    result->callback = callback;
    result->arg = arg;

    return result;
}

//**********************************************************************

Reactor *reactor_new(void) {
    Reactor *reactor;
    if ((reactor = malloc(sizeof(*reactor))) == NULL)
        abort();

    if ((reactor->epoll_fd = epoll_create1(0)) == -1) {
        perror("epoll_create1");
        free(reactor);
        return NULL;
    }

    reactor->table = g_hash_table_new_full(g_int_hash, g_int_equal, free, free);
    return reactor;
}

int reactor_destroy(Reactor *reactor) {
    if (close(reactor->epoll_fd) == -1) {
        perror("close");
        return -1;
    }

    GList *fds = g_hash_table_get_keys(reactor->table);
    void *next_fd;
    while ((next_fd = g_list_next(fds)) != NULL) {
        if (close(*(int *)next_fd) == -1)
            return -1;
    }

    g_list_free(fds);
    g_hash_table_destroy(reactor->table);
    free(reactor);

    return 0;
}

//**********************************************************************

#define REACTOR_CTL(reactor, op, fd, interest)                                 \
    if (epoll_ctl(reactor->epoll_fd, op, fd,                                   \
                  &(struct epoll_event){.events = interest,                    \
                                        .data = {.fd = fd}}) == -1) {          \
        perror("epoll_ctl");                                                   \
        return -1;                                                             \
    }

int reactor_register(const Reactor *reactor, int fd, uint32_t interest,
                     Callback callback, void *callback_arg) {
    REACTOR_CTL(reactor, EPOLL_CTL_ADD, fd, interest)
    g_hash_table_insert(reactor->table, int_in_heap(fd),
                        callback_data_new(callback, callback_arg));
    return 0;
}

int reactor_deregister(const Reactor *reactor, int fd) {
    REACTOR_CTL(reactor, EPOLL_CTL_DEL, fd, 0)
    g_hash_table_remove(reactor->table, &fd);
    return 0;
}

int reactor_reregister(const Reactor *reactor, int fd, uint32_t interest,
                       Callback callback, void *callback_arg) {
    REACTOR_CTL(reactor, EPOLL_CTL_MOD, fd, interest)
    g_hash_table_insert(reactor->table, int_in_heap(fd),
                        callback_data_new(callback, callback_arg));
    return 0;
}

//**********************************************************************

int reactor_run(const Reactor *reactor, time_t timeout) {
    int result;
    struct epoll_event *events;
    if ((events = calloc(MAX_EVENTS, sizeof(*events))) == NULL)
        abort();

    time_t start = time(NULL);

    while (true) {
        time_t passed = time(NULL) - start;
        int nfds =
            epoll_wait(reactor->epoll_fd, events, MAX_EVENTS, timeout - passed);

        switch (nfds) {
        // Ошибка
        case -1:
            perror("epoll_wait");
            result = -1;
            goto cleanup;
        // Время вышло
        case 0:
            result = 0;
            goto cleanup;
        // Успешная операция
        default:
            // Вызвать обработчиков событий
            for (int i = 0; i < nfds; i++) {
                int fd = events[i].data.fd;

                CallbackData *callback =
                    g_hash_table_lookup(reactor->table, &fd);
                callback->callback(callback->arg, fd, events[i].events);
            }
        }
    }

cleanup:
    free(events);
    return result;
}
