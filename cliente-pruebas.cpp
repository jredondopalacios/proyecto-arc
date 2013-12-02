#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <errno.h>
#include <thread>
#include <map>
#include <vector>
#include <string.h>
#include <unistd.h>
#include <mutex>
#include <iostream>
#include <unordered_map>
#include <assert.h>
#include <iostream>
#include <fstream>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <errno.h>
#include <cstring>
#include <vector>
#include <thread>
#include <unistd.h>
#include <string>
#include <arpa/inet.h>
#include <ctime>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sstream>
#include <mutex>
#include <map>
#include <assert.h>

#include "mensajes.h"

#define GRUPO_SIZE 		10
#define GRUPO_COUNT 	2500
#define THREAD_POOL		2
#define CICLOS			100

typedef struct _client_data {
	int id;
	int socket;
	int ack_pendiente;
	int32_t secuencia;
} client_data;


typedef int64_t msec_t;

msec_t time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (msec_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void client_thread(int epoll_fd)
{

}
using namespace std;

int main(int argc, char** argv)
{
	int epoll_fds[THREAD_POOL];

	for(int i = 0; i < THREAD_POOL; i++)
	{
		epoll_fds[i] = epoll_create1(0);
	}

	struct sockaddr_in      dir;
	dir.sin_family = PF_INET;
	dir.sin_port = htons(12345);
	inet_aton("127.0.0.1",&dir.sin_addr);

	int server_socket;

	mensaje_conexion nueva_conexion;
	mensaje_conexion_satisfactoria conexion_respuesta;
	uint8_t tipo_mensaje = MENSAJE_CONEXION;

	char buffer[40];

	memcpy(buffer, &tipo_mensaje, sizeof(uint8_t));

	int miembros_grupo[GRUPO_SIZE * GRUPO_COUNT];

	for(int i = 0; i < GRUPO_COUNT; i++)
	{

		for(int j = 0; j < GRUPO_SIZE; j++)
		{

			if ((server_socket = socket(PF_INET, SOCK_STREAM, 0))<0)
			{
				perror("socket() error");
				close(server_socket);
				return 0;
			}
			
			if (connect(server_socket, (struct sockaddr *)&dir, sizeof(struct sockaddr_in))<0)
			{
				perror("connect() error");
				close(server_socket);
				return 0;
			}

			nueva_conexion.grupo = i;
			memcpy(buffer, &tipo_mensaje, sizeof(uint8_t));
			memcpy(&buffer[1], &nueva_conexion, sizeof(mensaje_conexion));

			send(server_socket, buffer, sizeof(uint8_t) + sizeof(mensaje_conexion),0);
			recv(server_socket, buffer, sizeof(uint8_t) + sizeof(mensaje_conexion_satisfactoria),0);

			memcpy(&conexion_respuesta, &buffer[1], sizeof(conexion_respuesta));

			epoll_event client_event;
	    	client_data *data = (client_data * ) malloc(sizeof(client_data));
	    	data->id = conexion_respuesta.cliente_id;
	    	data->ack_pendiente = GRUPO_SIZE - 1;
	    	data->socket = server_socket;

	    	client_event.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
	    	client_event.data.ptr = data;

			epoll_ctl(epoll_fds[i % THREAD_POOL], EPOLL_CTL_ADD, server_socket, &client_event);

			//cout << "Cliente Num. " << j << " de GrupoID " << i << " se ha conectado al servidor con ID " << data->id << endl;

			miembros_grupo[j + i * GRUPO_SIZE] = server_socket;
		}

	}

	tipo_mensaje = MENSAJE_POSICION;
	memcpy(buffer, &tipo_mensaje, sizeof(tipo_mensaje));

	mensaje_posicion posicion;
	posicion.posicion_x = 0;
	posicion.posicion_y = 0;
	posicion.posicion_z = 0;
	posicion.numero_secuencia = 0;

	for(int j = 0; j < GRUPO_SIZE * GRUPO_COUNT; j++)
	{
		posicion.cliente_id_origen = miembros_grupo[j];
		memcpy(&buffer[1], &posicion, sizeof(posicion));
		send(server_socket, buffer, sizeof(uint8_t) + sizeof(posicion), 0);
	}

	return 0;
}