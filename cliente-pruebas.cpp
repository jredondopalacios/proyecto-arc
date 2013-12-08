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
#define GRUPO_COUNT 	1000
#define THREAD_POOL		4
#define CICLOS			100
#define MAXEVENTS		399999

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

using namespace std;

mutex report_mutex;

msec_t inicio_prueba;

void client_thread(int epoll_fd)
{
	int n_wait;

	struct epoll_event ev[MAXEVENTS];

	uint8_t buffer[400];
	client_data* data;
	uint8_t tipo_mensaje;
	mensaje_posicion posicion;
	mensaje_reconocimiento reconocimiento;

	do
	{
		n_wait = epoll_wait(epoll_fd, ev, MAXEVENTS, -1);

		for(int i = 0; i < n_wait; i++)
		{
			if(ev[i].events & EPOLLIN)
			{
				data = (client_data*) ev[i].data.ptr;

				recv(data->socket, buffer, sizeof(uint8_t), 0);

				//cout << "Recibido mensaje: " << buffer[0] << endl;

				switch(buffer[0])
				{
				case MENSAJE_POSICION:
					{
						recv(data->socket, &posicion, sizeof(posicion), 0);
						reconocimiento.cliente_id_origen = data->id;
						reconocimiento.cliente_id_destino = posicion.cliente_id_origen;

						//cout << posicion.cliente_id_origen << endl;
						assert(posicion.cliente_id_origen < 11000);


						/*report_mutex.lock();
						cout << "ID: " << data->id << " Reenviando reconocimiento a " << posicion.cliente_id_origen << endl;
						report_mutex.unlock();*/

						buffer[0] = MENSAJE_RECONOCIMIENTO;
						memcpy(&buffer[1], &reconocimiento, sizeof(reconocimiento));
						send(data->socket, buffer, sizeof(reconocimiento) + sizeof(uint8_t), 0);
						break;
					}
				case MENSAJE_RECONOCIMIENTO:
					{
						//report_mutex.lock();
						data->ack_pendiente -= 1;
						//cout << "[ID" << data->id << "] Recibido ACK-" << data->secuencia << ". Quedan " << data->ack_pendiente << endl;
						//report_mutex.unlock();
						recv(data->socket, &reconocimiento, sizeof(reconocimiento), 0);
						if(data->ack_pendiente == 0)
						{
							data->ack_pendiente = GRUPO_SIZE - 1;
							data->secuencia += 1;

							if(data->secuencia == CICLOS)
							{
								cout << "ClienteID " << data->id << " ha terminado con tiempo: " << (time_ms() - inicio_prueba) / 1000 << endl;
								break;
							}

							if(data->secuencia > CICLOS)
								break;

							buffer[0] = MENSAJE_POSICION;
							posicion.cliente_id_origen = data->id;
							memcpy(&buffer[1], &posicion, sizeof(posicion));
							send(data->socket, buffer, sizeof(uint8_t) + sizeof(posicion), 0);
							//cout << "Empezando nuevo ciclo. ID: " << data->id << endl;

						}

						break;
					}
				}
			}
		}
	}while(1);

}

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
	int clientes_id[GRUPO_SIZE * GRUPO_COUNT];

	msec_t inicio = time_ms();

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
			recv(server_socket, buffer, sizeof(uint8_t) + sizeof(conexion_respuesta),0);

			memcpy(&conexion_respuesta, &buffer[1], sizeof(conexion_respuesta));

			epoll_event client_event;
	    	client_data *data = (client_data * ) malloc(sizeof(client_data));
	    	data->id = conexion_respuesta.cliente_id;
	    	data->ack_pendiente = GRUPO_SIZE - 1;
	    	data->socket = server_socket;
	    	data->secuencia = 0;

	    	client_event.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
	    	client_event.data.ptr = data;

			epoll_ctl(epoll_fds[i % THREAD_POOL], EPOLL_CTL_ADD, server_socket, &client_event);

			//cout << "Cliente Num. " << j << " de GrupoID " << i << " se ha conectado al servidor con ID " << data->id << endl;

			miembros_grupo[j + i * GRUPO_SIZE] = server_socket;
			clientes_id[j + i * GRUPO_SIZE] = conexion_respuesta.cliente_id;
		}

	}

	msec_t fin = time_ms();
	msec_t total = (fin - inicio)/1000;
	//cout << "Conectados " << GRUPO_SIZE * GRUPO_COUNT << " clientes en " << total << " segundos." << endl;
	//cout << "Conexiones por segundo: " << ((GRUPO_COUNT*GRUPO_SIZE) / total) << endl;

	tipo_mensaje = MENSAJE_POSICION;
	memcpy(buffer, &tipo_mensaje, sizeof(tipo_mensaje));

	mensaje_posicion posicion;
	posicion.posicion_x = 0;
	posicion.posicion_y = 0;
	posicion.posicion_z = 0;
	posicion.numero_secuencia = 0;

	inicio = time_ms();

	for(int j = 0; j < GRUPO_SIZE * GRUPO_COUNT; j++)
	{
		posicion.cliente_id_origen = clientes_id[j];
		//cout << "Enviando posicion con ID Origen: " << clientes_id[j] << endl;
		memcpy(&buffer[1], &posicion, sizeof(posicion));
		send(miembros_grupo[j], buffer, sizeof(uint8_t) + sizeof(posicion), 0);
	}

	fin = time_ms();
	total = (fin - inicio);

	//cout << "Los clientes han empezado su primer ciclo." << endl;

	thread thread_pool[THREAD_POOL];

	inicio_prueba = time_ms();

	for(int i = 0; i < THREAD_POOL; i++)
	{
		thread_pool[i] = thread(client_thread,epoll_fds[i]);
	}

	cin >> buffer;

	return 0;
}