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

#define ERR_SOCKET_READ					-1
#define ERR_CLOSE				        -2
#define ERR_BLOCK_READ					-3

#define LISTEN_QUEUE 				1024
#define BUFFER_SIZE 				24

using namespace std;

int aio_socket_escucha(int puerto);
grupoid_t aio_lectura_grupo(int socket);

#endif
