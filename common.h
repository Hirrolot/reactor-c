#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "reactor.h"

//************************************************************

#define SERVER_PORT 18470
#define SERVER_IPV4 "127.0.0.1"
#define SERVER_BACKLOG 1024
#define SERVER_TIMEOUT_MILLIS 1000 * 60

#define REQUEST_BUFFER_CAPACITY 1024

#define CRLF "\r\n"
#define DOUBLE_CRLF CRLF CRLF
#define DOUBLE_CRLF_LEN strlen(DOUBLE_CRLF)

//************************************************************

#define SAFE_CALL(call, error)                                                 \
    do {                                                                       \
        if ((call) == error) {                                                 \
            fail("%s", #call);                                                 \
        }                                                                      \
    } while (false)

//************************************************************

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
 * Обработчик событий, который вызовется после того, как сокет будет
 * готов принять часть HTTP запроса.
 */
static void on_recv(void *arg, int fd, uint32_t events);

/*
 * Переводит входящее соединение в неблокирующий режим.
 */
static void set_nonblocking(int fd);

/*
 * Печатает переданные аргументы в stderr и выходит из процесса с
 * кодом `EXIT_FAILURE`.
 */
static noreturn void fail(const char *format, ...);

/*
 * Возвращает файловый дескриптор сокета, способного принимать новые
 * TCP соединения.
 */
static int new_server(bool reuse_port);

//************************************************************

static noreturn void fail(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, ": %s\n", strerror(errno));
    exit(EXIT_FAILURE);
}

//************************************************************

typedef struct {
    char data[REQUEST_BUFFER_CAPACITY];
    size_t size;
} RequestBuffer;

static RequestBuffer *request_buffer_new(void) {
    RequestBuffer *result = malloc(sizeof(*result));
    result->size = 0;
    return result;
}

static void request_buffer_destroy(RequestBuffer *buffer) { free(buffer); }

static bool request_buffer_is_complete(RequestBuffer *buffer) {
    return buffer->size < DOUBLE_CRLF_LEN
               ? false
               : strstr(buffer->data, DOUBLE_CRLF) != NULL;
}

static void request_buffer_clear(RequestBuffer *buffer) { buffer->size = 0; }

//************************************************************

static void set_nonblocking(int fd) {
    int flags;
    SAFE_CALL((flags = fcntl(fd, F_GETFL, 0)), -1);
    SAFE_CALL(fcntl(fd, F_SETFL, flags | O_NONBLOCK), -1);
}

//************************************************************

static void on_accept(void *arg, int fd, uint32_t events) {
    int incoming_conn;
    SAFE_CALL((incoming_conn = accept(fd, NULL, NULL)), -1);
    set_nonblocking(incoming_conn);
    SAFE_CALL(reactor_register(reactor, incoming_conn, EPOLLIN, on_recv,
                               request_buffer_new()),
              -1);
}

static void on_recv(void *arg, int fd, uint32_t events) {
    RequestBuffer *buffer = arg;

    // Принимаем входные данные до тех пор, что recv возвратит 0 или ошибку
    ssize_t nread;
    while ((nread = recv(fd, buffer->data + buffer->size,
                         REQUEST_BUFFER_CAPACITY - buffer->size, 0)) > 0)
        buffer->size += nread;

    // Клиент оборвал соединение
    if (nread == 0) {
        SAFE_CALL(reactor_deregister(reactor, fd), -1);
        SAFE_CALL(close(fd), -1);
        request_buffer_destroy(buffer);
        return;
    }

    // read вернул ошибку, отличную от ошибки, при которой вызов заблокирует
    // поток
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        request_buffer_destroy(buffer);
        fail("read");
    }

    // Получен полный HTTP запрос от клиента. Теперь регистрируем обработчика
    // событий для отправки данных
    if (request_buffer_is_complete(buffer)) {
        request_buffer_clear(buffer);
        SAFE_CALL(reactor_reregister(reactor, fd, EPOLLOUT, on_send, buffer),
                  -1);
    }
}

static void on_send(void *arg, int fd, uint32_t events) {
    const char *content = "<img "
                          "src=\"https://habrastorage.org/webt/oh/wl/23/"
                          "ohwl23va3b-dioerobq_mbx4xaw.jpeg\">";
    char response[1024];
    sprintf(response,
            "HTTP/1.1 200 OK" CRLF "Content-Length: %zd" CRLF "Content-Type: "
            "text/html" DOUBLE_CRLF "%s",
            strlen(content), content);

    SAFE_CALL(send(fd, response, strlen(response), 0), -1);
    SAFE_CALL(reactor_reregister(reactor, fd, EPOLLIN, on_recv, arg), -1);
}

//************************************************************

static int new_server(bool reuse_port) {
    int fd;
    SAFE_CALL((fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP)),
              -1);

    if (reuse_port) {
        SAFE_CALL(
            setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int)),
            -1);
    }

    struct sockaddr_in addr = {.sin_family = AF_INET,
                               .sin_port = htons(SERVER_PORT),
                               .sin_addr = {.s_addr = inet_addr(SERVER_IPV4)},
                               .sin_zero = {0}};

    SAFE_CALL(bind(fd, (struct sockaddr *)&addr, sizeof(addr)), -1);
    SAFE_CALL(listen(fd, SERVER_BACKLOG), -1);
    return fd;
}