#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "reactor.h"

#define SERVER_PORT 18474
#define SERVER_IPV4 "127.0.0.1"
#define SERVER_BACKLOG 1024
#define SERVER_TIMEOUT_MILLIS 1000 * 60

/*
 * Обработчик событий, который вызовется после того, как сокет будет
 * готов принять новое соединение.
 */
static void on_accept(void *arg, int fd, uint32_t events);

/*
 * Обработчик событий, который вызовется после того, как сокет будет
 * готов отправить HTTP ответ.
 */
static void on_send(void *arg, int fd, uint32_t events);

/*
 * Печатает переданные аргументы в stderr и выходит из процесса с
 * кодом `EXIT_FAILURE`.
 */
static noreturn void fail(const char *format, ...);

/*
 * Возвращает файловый дескриптор сокета, способного принимать новые
 * TCP соединения.
 */
static int new_server(void);

static Reactor *reactor;

int main(void) {
    if ((reactor = reactor_new()) == NULL)
        fail("reactor_new");

    if (reactor_register(reactor, new_server(), EPOLLIN, on_accept, NULL) == -1)
        fail("reactor_register");

    if (reactor_run(reactor, SERVER_TIMEOUT_MILLIS) == -1)
        fail("reactor_run");

    if (reactor_destroy(reactor) == -1)
        fail("reactor_destroy");

    return EXIT_SUCCESS;
}

static noreturn void fail(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, ": %s\n", strerror(errno));
    exit(EXIT_FAILURE);
}

static void on_accept(void *arg, int fd, uint32_t events) {
    int incoming_conn;
    if ((incoming_conn = accept(fd, NULL, NULL)) == -1)
        perror("accept");
    else
        reactor_register(reactor, incoming_conn, EPOLLOUT, on_send, NULL);
}

static void on_send(void *arg, int fd, uint32_t events) {
    const char *content = "<img "
                          "src=\"https://habrastorage.org/webt/oh/wl/23/"
                          "ohwl23va3b-dioerobq_mbx4xaw.jpeg\">";

    char response[1024];
    sprintf(response,
            "HTTP/1.1 200 OK\r\nContent-Length: %zd\r\nContent-Type: "
            "text/html\r\nConnection: Closed\r\n\r\n%s",
            strlen(content), content);

    if (send(fd, response, strlen(response), 0) == -1)
        perror("send");

    reactor_deregister(reactor, fd);

    if (close(fd) == -1)
        perror("close");
}

static int new_server(void) {
    int server;
    if ((server = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP)) ==
        -1)
        fail("socket");

    struct sockaddr_in addr = {.sin_family = AF_INET,
                               .sin_port = htons(SERVER_PORT),
                               .sin_addr = {.s_addr = inet_addr(SERVER_IPV4)},
                               .sin_zero = {0}};

    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) == -1)
        fail("bind");

    if (listen(server, SERVER_BACKLOG) == -1)
        fail("listen");

    return server;
}