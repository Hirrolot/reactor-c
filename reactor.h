#ifndef REACTOR_H
#define REACTOR_H

#include <stdint.h>
#include <time.h>

/*
 * Указатель на функцию, которая будет вызываться I/O реактором при поступлении
 * события от системного селектора.
 */
typedef void (*Callback)(void *arg, int fd, uint32_t events);

typedef struct reactor Reactor;

/*
 * Возвращает `NULL` в случае ошибки, не-`NULL` указатель на `Reactor` в
 * противном случае.
 */
Reactor *reactor_new(void);

/*
 * Освобождает системный селектор, все зарегистрированные сокеты в данный момент
 * времени и сам I/O реактор.
 *
 * Следующие функции возвращают -1 в случае ошибки, 0 в случае успеха.
 */
int reactor_destroy(Reactor *reactor);

int reactor_register(const Reactor *reactor, int fd, uint32_t interest,
                     Callback callback, void *callback_arg);
int reactor_deregister(const Reactor *reactor, int fd);
int reactor_reregister(const Reactor *reactor, int fd, uint32_t interest,
                       Callback callback, void *callback_arg);

/*
 * Запускает цикл событий с тайм-аутом `timeout`.
 *
 * Эта функция передаст управление вызывающему коду если отведённое время вышло
 * или/и при отсутствии зарегистрированных сокетов.
 */
int reactor_run(const Reactor *reactor, time_t timeout);

#endif // REACTOR_H
