/**********************************************************************************************************/
/*                                                                                                        */
/*                           ~ Proyecto Arquitectura de Redes de Computadores ~                           */
/*                                                                                                        */
/*   Fichero: servidor.cpp                                             Autor: Jorge Redondo Palacios      */
/*   Licencia: GPL V2                                                                                     */
/*                                                                                                        */
/*    ----- Arquitectura del Servidor:                                                                    */


#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
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

#include "mensajes.h"
#include "network.h"

#define SERVER_PORT  12345
#define MAXEVENTS	 30000

#define TRUE             1
#define FALSE            0

#define MAX_GRUPOS 10000
#define THREAD_POOL 1024


#define _DEBUG_




using namespace std;

struct grupo_key {
	grupoid_t grupoid;
};

struct grupo_hash {
	size_t operator() (const grupo_key& g) const
	{
		return g.grupoid;
	}
};

struct grupo_hash_equal {
	bool operator() (const grupo_key& lkey, const grupo_key& rkey) const
	{
		return lkey.grupoid == rkey.grupoid;
	}
};

typedef vector<struct epoll_data_client *> vector_cliente;
unordered_map<grupo_key, vector_cliente, grupo_hash, grupo_hash_equal> clientes_grupo;

int clientes_conectados = 0;


void worker_thread(int epollfd)
{
	struct epoll_event event;
   	struct epoll_event epoll_events[MAXEVENTS];

   	int epoll_n;

   	do
   {
   		epoll_n = epoll_wait(epollfd, epoll_events, MAXEVENTS, -1);

		for (int i = 0; i < epoll_n; i++)
		{

		    //cout << "---------------------------------" << endl;
		    if ((epoll_events[i].events & EPOLLRDHUP) || (epoll_events[i].events & EPOLLHUP) || (epoll_events[i].events & EPOLLERR))
		    {
		    	struct epoll_data_client * data_client = (struct epoll_data_client *) epoll_events[i].data.ptr;
		    	close(data_client->socketfd);
		    	cout << "Desconectado ClienteID: " << data_client->socketfd << " del GrupoID: " << data_client->grupoid << endl << flush;

		    	struct grupo_key key;
	    		struct mensaje_desconexion desconexion;
	    		char buffer_mensaje[40];
	    		mensaje_t tipo_mensaje = MENSAJE_DESCONEXION;

	    		key.grupoid = data_client->grupoid;
	    		vector_cliente clientes = clientes_grupo[key];

	    		int erase_index;
	    		bool erase_find = false;

	    		desconexion.cliente_id_origen = data_client->socketfd;
	    		memcpy(buffer_mensaje, &tipo_mensaje, sizeof(mensaje_t));
	    		memcpy(&buffer_mensaje[1], &desconexion, sizeof(struct mensaje_desconexion));

	    		cout << "En el grupo había " << clientes_grupo[key].size() << " clientes." << endl;

	    		for(uint i = 0; i < clientes.size(); i++)
	    		{
	    			if(((struct epoll_data_client *) clientes[i])->socketfd != data_client->socketfd)
	    			{
	    				cout << "Enviando información de desconexión sobre " << data_client->socketfd << " a " << clientes[i] << endl;
	    				async_write(clientes[i], buffer_mensaje, sizeof(mensaje_t) + sizeof(struct mensaje_desconexion));
	    			}
	    			else
	    			{
	    				cout << "Se ha encontrado ID " << ((struct epoll_data_client *) clientes[i])->socketfd << " en el vector";
	    				cout << " en el índice " << i + 1 << "/" << clientes.size() << endl;
	    				erase_index = i;
	    				erase_find = true;
	    			}
	    		}

	    		if (erase_find)
	    		{
	    			cout << "Borrada ID " << data_client->socketfd << " del vector de clientes de grupo." << endl;
	    			clientes_grupo[key].erase(erase_index + clientes_grupo[key].begin());
	    		}

	    		cout << "El GrupoID " << key.grupoid << " tiene ahora " << clientes_grupo[key].size() << endl;

	    		clientes_conectados--;

	    		cout << "Hay en total " << clientes_conectados << " clientes conectados en el sistema." << endl;

	    		continue;
		    }

		    if (epoll_events[i].events & EPOLLOUT)
		    {
		    	async_write_delay((struct epoll_data_client *) epoll_events[i].data.ptr);
		    }

		    if (epoll_events[i].events & EPOLLIN)
		    {
		    	int rc;

		    	do
		    	{
			    	mensaje_t tipo_mensaje;
			    	char buffer_mensaje[400];
			    	struct epoll_data_client * data_client = (struct epoll_data_client *) epoll_events[i].data.ptr;


			    	rc = async_read(data_client, buffer_mensaje, 40);

			    	if(rc == READ_ERROR || rc == READ_CLOSE)
			    	{
			    		printf("async_read() error\n");
			    		struct epoll_data_client * data_client = (struct epoll_data_client *) epoll_events[i].data.ptr;
				    	close(data_client->socketfd);
				    	cout << "Desconectado ClienteID: " << data_client->socketfd << " del GrupoID: " << data_client->grupoid << endl << flush;

				    	struct grupo_key key;
			    		struct mensaje_desconexion desconexion;
			    		char buffer_mensaje[40];
			    		mensaje_t tipo_mensaje = MENSAJE_DESCONEXION;

			    		key.grupoid = data_client->grupoid;
			    		vector_cliente clientes = clientes_grupo[key];

			    		int erase_index;
			    		bool erase_find = false;

			    		desconexion.cliente_id_origen = data_client->socketfd;
			    		memcpy(buffer_mensaje, &tipo_mensaje, sizeof(mensaje_t));
			    		memcpy(&buffer_mensaje[1], &desconexion, sizeof(struct mensaje_desconexion));

			    		cout << "En el grupo había " << clientes_grupo[key].size() << " clientes." << endl;

			    		for(uint i = 0; i < clientes.size(); i++)
			    		{
			    			if(((struct epoll_data_client *) clientes[i])->socketfd != data_client->socketfd)
			    			{
			    				cout << "Enviando información de desconexión sobre " << data_client->socketfd << " a " << ((struct epoll_data_client *)clientes[i])->socketfd << endl;
			    				async_write(clientes[i], buffer_mensaje, sizeof(mensaje_t) + sizeof(struct mensaje_desconexion));
			    			}
			    			else
			    			{
			    				cout << "Se ha encontrado ID " << ((struct epoll_data_client *) clientes[i])->socketfd << " en el vector";
			    				cout << " en el índice " << i + 1 << "/" << clientes.size() << endl;
			    				erase_index = i;
			    				erase_find = true;
			    			}
			    		}

			    		if (erase_find)
			    		{
			    			cout << "Borrada ClienteID: " << data_client->socketfd << " del vector de clientes de grupo." << endl;
			    			clientes_grupo[key].erase(erase_index + clientes_grupo[key].begin());
			    		}

			    		cout << "El GrupoID " << key.grupoid << " tiene ahora " << clientes_grupo[key].size() << endl;

			    		clientes_conectados--;

			    		cout << "Hay en total " << clientes_conectados << " clientes conectados en el sistema." << endl;

			    		break;


				    	}

			    	if(rc == READ_BLOCK)
			    	{
			    		break;
			    	}

					switch(buffer_mensaje[0])
					{
						case MENSAJE_SALUDO:
						{
#ifdef _DEBUG_
							printf("Recibido saludo de ID: %d. GrupoID: %d\n", data_client->socketfd, data_client->grupoid);
#endif
							struct grupo_key key;
							key.grupoid = data_client->grupoid;
							vector_cliente clientes = clientes_grupo[key];

							for(uint i = 0; i < clientes.size(); i++)
							{
								if(((struct epoll_data_client *) clientes[i])->socketfd != data_client->socketfd)
								{
									async_write(clientes[i], buffer_mensaje, sizeof(mensaje_t) + sizeof(struct mensaje_saludo));
								}
							}
							break;
						}
						case MENSAJE_POSICION:
						{
#ifdef _DEBUG_
							//printf("Recibida posicion de ID: %d. GrupoID: %d\n", data_client->socketfd, data_client->grupoid);
#endif
							struct grupo_key key;
							key.grupoid = data_client->grupoid;
							vector_cliente clientes = clientes_grupo[key];

							struct mensaje_posicion posicion;
							memcpy(&posicion, &buffer_mensaje[1], sizeof(posicion));

							//posicion = (struct mensaje_posicion) (* (&buffer_mensaje))

							assert(posicion.cliente_id_origen < 11000);
							//cout << "Reenviando a " << clientes.size() << " clientes..." << endl;

							for(uint i = 0; i < clientes.size(); i++)
							{
								if(((struct epoll_data_client *) clientes[i])->socketfd != data_client->socketfd)
								{
									//send(clientes[i]->socketfd, buffer_mensaje, sizeof(mensaje_t) + sizeof(struct mensaje_posicion), MSG_NOSIGNAL | MSG_WAITALL);
									if (async_write(clientes[i], buffer_mensaje, sizeof(mensaje_t) + sizeof(struct mensaje_posicion)) < 0)
									{

										cout << "Error enviando a ID " << ((struct epoll_data_client *) clientes[i])->socketfd << endl;

										struct epoll_data_client * data_client = (struct epoll_data_client *) clientes[i];
										close(data_client->socketfd);
										cout << "Desconectado ClienteID: " << data_client->socketfd << " del GrupoID: " << data_client->grupoid << endl << flush;

										struct grupo_key key;
										struct mensaje_desconexion desconexion;
										char buffer_mensaje[40];
										mensaje_t tipo_mensaje = MENSAJE_DESCONEXION;

										key.grupoid = data_client->grupoid;
										vector_cliente clientes = clientes_grupo[key];

										int erase_index;
										bool erase_find = false;

										desconexion.cliente_id_origen = data_client->socketfd;
										memcpy(buffer_mensaje, &tipo_mensaje, sizeof(mensaje_t));
										memcpy(&buffer_mensaje[1], &desconexion, sizeof(struct mensaje_desconexion));

										cout << "En el grupo había " << clientes_grupo[key].size() << " clientes." << endl;

										for(uint i = 0; i < clientes.size(); i++)
										{
											if(((struct epoll_data_client *) clientes[i])->socketfd != data_client->socketfd)
											{
												cout << "Enviando información de desconexión sobre " << data_client->socketfd << " a " << clientes[i] << endl;
												async_write(clientes[i], buffer_mensaje, sizeof(mensaje_t) + sizeof(struct mensaje_desconexion));
											}
											else
											{
												cout << "Se ha encontrado ID " << ((struct epoll_data_client *) clientes[i])->socketfd << " en el vector";
												cout << " en el índice " << i + 1 << "/" << clientes.size() << endl;
												erase_index = i;
												erase_find = true;
											}
										}

										if (erase_find)
										{
											cout << "Borrada ID " << data_client->socketfd << " del vector de clientes de grupo." << endl;
											clientes_grupo[key].erase(erase_index + clientes_grupo[key].begin());
										}

										cout << "El GrupoID " << key.grupoid << " tiene ahora " << clientes_grupo[key].size() << endl;

										clientes_conectados--;

										cout << "Hay en total " << clientes_conectados << " clientes conectados en el sistema." << endl;

									}
								}
							}
							break;
						}

						case MENSAJE_RECONOCIMIENTO:
						{
							struct mensaje_reconocimiento reconocimiento;
							memcpy(&reconocimiento, &buffer_mensaje[1], sizeof(struct mensaje_reconocimiento));

							//printf("Recibido reconocimiento de ID %d a ID %d. GrupoID: %d\n", data_client->socketfd, reconocimiento.cliente_id_destino, data_client->grupoid);

							struct grupo_key key;
							key.grupoid = data_client->grupoid;
							vector_cliente clientes = clientes_grupo[key];

							assert(reconocimiento.cliente_id_destino < 11000);

							for(uint i = 0; i < clientes.size(); i++)
							{
								if(((struct epoll_data_client *) clientes[i])->socketfd == reconocimiento.cliente_id_destino)
								{
									async_write(clientes[i], buffer_mensaje, sizeof(mensaje_t) + sizeof(struct mensaje_reconocimiento));
								}
							}
							break;
						}

						case MENSAJE_NOMBRE_REPLY:
						{
							struct mensaje_nombre_reply nombre_reply;
							memcpy(&nombre_reply, &buffer_mensaje[1], sizeof(struct mensaje_nombre_reply));

							struct grupo_key key;
							key.grupoid = data_client->grupoid;
							vector_cliente clientes = clientes_grupo[key];

							for(uint i = 0; i < clientes.size(); i++)
							{
								if(((struct epoll_data_client *) clientes[i])->socketfd == nombre_reply.cliente_id_destino)
								{
									async_write(clientes[i], buffer_mensaje, sizeof(mensaje_t) + sizeof(struct mensaje_nombre_reply));
								}
							}
							break;
						}

						case MENSAJE_NOMBRE_REQUEST:
						{
							struct mensaje_nombre_request nombre_request;
							memcpy(&nombre_request, &buffer_mensaje[1], sizeof(struct mensaje_nombre_request));

							struct grupo_key key;
							key.grupoid = data_client->grupoid;
							vector_cliente clientes = clientes_grupo[key];

							for(uint i = 0; i < clientes.size(); i++)
							{
								if(((struct epoll_data_client *) clientes[i])->socketfd == nombre_request.cliente_id_destino)
								{
									async_write(clientes[i], buffer_mensaje, sizeof(mensaje_t) + sizeof(struct mensaje_nombre_request));
								}
							}
							break;
						}

					}

				} while(rc > 0);
			}
		}
	} while(TRUE);
}

int main (int argc, char *argv[])
{
   int    listen_sd, epoll_fd;
   struct epoll_event event;
   struct epoll_event epoll_events[MAXEVENTS];
   int epoll_fd_threads[THREAD_POOL];
   thread thread_pool[THREAD_POOL];

   listen_sd = aio_socket_escucha(SERVER_PORT);
   epoll_fd = epoll_create1(0);

   event.data.fd = listen_sd;
   event.events = EPOLLIN;


   for(int i=0; i < THREAD_POOL; i++)
   {
   		epoll_fd_threads[i] = epoll_create1(0);
   		thread_pool[i] = thread(worker_thread, epoll_fd_threads[i]);
   }

   epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sd, &event);

   int epoll_n;

   do
   {
   		epoll_n = epoll_wait(epoll_fd, epoll_events, MAXEVENTS, -1);

		for (int i = 0; i < epoll_n; i++)
		{

		    if (epoll_events[i].events & EPOLLIN)
		    {

			    if( epoll_events[i].data.fd == listen_sd)
			    {
#ifdef _DEBUG_
			    	printf("Recibida nueva conexión.\n");
#endif
			    	int new_client_sd;

			    	do
				    {
				    	struct sockaddr_in new_client_sockaddr;
		    			socklen_t clientsize = sizeof(new_client_sockaddr);
				    	new_client_sd = accept4(listen_sd, (struct sockaddr *)&new_client_sockaddr, &clientsize, SOCK_NONBLOCK);
				    	if(new_client_sd < 0)
				    	{
				    		if(errno != EWOULDBLOCK || errno != EAGAIN)
				    		{
				    			perror("accept4()");
				    		}
				    		break;
				    	}

				    	epoll_event client_event;
				    	epoll_data_client *data = (epoll_data_client * ) malloc(sizeof(struct epoll_data_client));
				    	init_epoll_data(new_client_sd, data);
				    	client_event.events = /*EPOLLOUT|*/ EPOLLIN | EPOLLET| EPOLLRDHUP | EPOLLHUP | EPOLLERR;
				    	client_event.data.ptr = data;
#ifdef _DEBUG_
			    		cout << "Nuevo cliente en socket: " << new_client_sd << endl <<flush;
#endif
			    		int index = new_client_sd % THREAD_POOL;

				    	if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_client_sd, &client_event) < 0)
				    	{
				    		perror("epoll_ctl()");
				    		close(new_client_sd);
				    		continue;
				    	}


				    } while (new_client_sd >= 0);
				} else {
					mensaje_t tipo_mensaje = 0;
					struct epoll_data_client * data_client = (struct epoll_data_client *) epoll_events[i].data.ptr;
					read(data_client->socketfd, &tipo_mensaje, sizeof(mensaje_t));
					if(tipo_mensaje == MENSAJE_CONEXION)
					{
						char buffer_mensaje[40];
						struct mensaje_conexion nueva_conexion;
						int rc = read(data_client->socketfd, &nueva_conexion, sizeof(struct mensaje_conexion));

						struct grupo_key key;
						key.grupoid = nueva_conexion.grupo;

						clientes_conectados++;
#ifdef _DEBUG_
						printf("Recibida petición a GrupoID: %d. Socket: %d\n", key.grupoid, data_client->socketfd);
						printf("Clientes conectados: %d\n", clientes_conectados);
						printf("Grupos activos: %d\n\n", clientes_grupo.size());
#endif
						mensaje_t tipo_mensaje = MENSAJE_CONEXION_SATISFACTORIA;
						struct mensaje_conexion_satisfactoria conexion_satisfactoria;
						conexion_satisfactoria.cliente_id = data_client->socketfd;

						memcpy(buffer_mensaje, &tipo_mensaje, sizeof(mensaje_t));
						memcpy(buffer_mensaje + sizeof(mensaje_t), &conexion_satisfactoria, sizeof(struct mensaje_conexion_satisfactoria));

						write(data_client->socketfd, buffer_mensaje, sizeof(mensaje_t) + sizeof(struct mensaje_conexion_satisfactoria));

						int index = nueva_conexion.grupo % THREAD_POOL;
						epoll_ctl(epoll_fd, EPOLL_CTL_DEL, data_client->socketfd, NULL);

						epoll_data_client *data = (epoll_data_client * ) malloc(sizeof(struct epoll_data_client));
				    	init_epoll_data(data_client->socketfd, data);
				    	epoll_event client_event;
				    	client_event.events = EPOLLOUT | EPOLLIN | EPOLLET| EPOLLRDHUP | EPOLLHUP | EPOLLERR;
				    	client_event.data.ptr = data;

				    	data->grupoid = nueva_conexion.grupo;

						

						clientes_grupo[key].push_back(data);


						epoll_ctl(epoll_fd_threads[index], EPOLL_CTL_ADD, data_client->socketfd, &client_event);
					}
				}
		    }
		}
    } while (TRUE);

    close(listen_sd);

    return 0;
}
