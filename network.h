#ifndef _NETWORK_H_
#define _NETWORK_H_

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <fcntl.h>

#include "mensajes.h"

#define READ_CLOSE						-2
#define READ_ERROR						-1
#define READ_BLOCK				        0
#define READ_SUCCESS					1

#define LISTEN_QUEUE 					1024
#define INITIAL_BUFFER_SIZE				20000

using namespace std;

struct epoll_data_client {
	int 			socketfd;
	grupoid_t		grupoid;
	char 			write_buffer[INITIAL_BUFFER_SIZE];
	char 			read_buffer[INITIAL_BUFFER_SIZE];
	char			*read_buffer_ptr, *write_buffer_ptr;
	int 			read_count, write_count, read_count_total;
	bool			tipo_mensaje_read;
};

int aio_socket_escucha(int puerto);
int async_write(struct epoll_data_client* data, void * buffer, int length);
int async_write_delay(struct epoll_data_client* data);
int async_read(struct epoll_data_client * data, void * buffer, int length);
void init_epoll_data(int socketfd, struct epoll_data_client * data);


#endif
