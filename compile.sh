#/bin/sh

gcc http_server.c reactor.c -Wall -std=c11 -O3 -o http_server \
    `pkg-config --cflags --libs glib-2.0`

gcc http_server_multithreaded.c reactor.c -Wall -std=c11 -O3 \
	-o http_server_multithreaded \
    `pkg-config --cflags --libs glib-2.0` -fopenmp
