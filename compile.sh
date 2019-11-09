#/bin/sh

gcc main_single_threaded.c reactor.c -Wall -std=c11 `pkg-config --cflags --libs glib-2.0` -o main_single_threaded -O3
