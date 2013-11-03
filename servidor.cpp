/**********************************************************************************************************/
/*                                                                                                        */
/*                           ~ Proyecto Arquitectura de Redes de Computadores ~                           */
/*                                                                                                        */
/*   Fichero: servidor.cpp                                             Autor: Jorge Redondo Palacios      */
/*   Licencia: GPL V2                                                                                     */
/*                                                                                                        */
/*    ----- Arquitectura del Servidor:                                                                    */
/*                                                                                                        */
/*        El servidor desarrollado se trata de un servidor multi-hilo, con sockets bloqueantes con el     */
/*    objetivo de alcanzar altas prestaciones. Se han hecho varias decisiones de diseño para alcanzar     */
/*    dicha meta, o al menos conseguir el mayor rendimiento posible.                                      */
/*                                                                                                        */
/*        En primer lugar, se ha optado por una arquitectura multi-hilo ya que se ajusta perfectamente    */
/*    al tipo de sistema a desarrollar. Puesto que los clientes se dividirán en grupos, y nunca inter-    */
/*    accionarán clientes de distintos grupos, cada hilo puede guardar en local una lista de todos sus    */
/*    clientes conectados. Aparte de que ningún hilo debería acceder a la lista de miembros de otros      */
/*    hilos (Es decir, el funcionamiento de un grupo es independiente del de los demás), restringimos     */
/*    la búsqueda de las IDs, haciendo desaparecer el coste de buscar a qué grupo pertenecia cada pa-     */
/*    quete entrante (Que tendríamos en una arquitectura de un solo hilo); búsqueda con un coste en ca-   */
/*    so peor que era lineal con el número total de clientes entre todos los grupos.                      */
/*                                                                                                        */
/*        Se ha optado por utilizar sockets bloqueantes, ya que, por la propia naturaleza del sistema,    */
/*    no se tiene la necesidad de realizar otras tareas mientras esperamos que se envíen o lean datos,    */
/*    y si se utilizasen sockets no bloqueantes, el SO mantendría constantemente el proceso activo por    */
/*    culpa del polling producido. En cambio, con los sockets bloqueantes utilizados, el proceso sólo     */
/*    es despertado cuando termina la operación, permitiendo continuar. Se hubieran podido utilizar       */
/*    sockets no bloqueantes y paralelizar el reenvío de mensajes a los miembros de un grupo, pero        */
/*    creemos que con muchos clientes obtendríamos un peor rendimiento por culpa del overhead de lanzar   */
/*    tantos hilos como clientes de un grupo, y destruirlos a los pocos milisegundos.                     */
/*                                                                                                        */
/*		  Además se ha abandonado el uso de la sentencia select() debido a su bajo rendimiento y escala-  */
/*	  bilidad. En su lugar utilizamos epoll, un sistema sólo de Linux presente desde la versión del       */
/*	  kernel 2.6. epoll no sólo elimina el límite que tenía select() en cuanto a total de descriptores,   */
/*	  sino que además el coste de select() era lineal con el número de descriptores a comprobar, mientras */
/*	  que el de epoll es lineal. Esto provoca que la aplicación sea mucho más escalable, más eficiente,   */
/*	  y soporte más clientes.                                                                             */
/*                                                                                                        */
/**********************************************************************************************************/


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

#include "mensajes.h"

#define SERVER_PORT  12345
#define MAXEVENTS	 10000

#define TRUE             1
#define FALSE            0



using namespace std;

/* Almacenaremos todos los hilos creados para los grupos en un vector, para, cuando terminemos, esperar a que
estos hilos terminen antes */
vector<thread> grupos_hilos;

mutex mtx;

/* Los descriptores de fichero de epoll permitirán al hilo principal actualizarlos cuando entre nuevas conexiones que
se unan a los grupos Este descriptor se le pasará al hilo para que espere sobre él. Se ha optado por un contendor de tipo
map<key,value> por su coste de búsqueda de log(n). Aún no siendo una operación crítica, sí que va a haber más accesos a
las estructuras fd_set que inserciones de nuevos grupos, por lo que interesa mantenerlo en un contendeor ordenado y con
índice binario para búsqueda */
map<_grupo_id,int> grupos_sets;

inline _tipo_mensaje recv_tipo_mensaje(int sd) {
	_tipo_mensaje tipo;
	int rc;
	rc = recv(sd, &tipo, sizeof(tipo),0);
	if (rc < 0)
	{
		return rc;
	}
	if (rc == 0) 
	{
		return 0;
	}
	return tipo;
}

/* grupo_thread es la función que ejecutará cada hilo. Cada hilo se le asgina a un grupo y
es el único encargado de pasar todos los mensajes de los miembros del grupo, de esta forma
sólo el hilo es consciente de lo que pasa en cada grupo así de sus miembros conectados, 
aislando cada hilo las tareas de gestión de mensajes de los grupos. Es la principal herramienta
para paralelizar las tareas del servidor. */
void grupo_thread (int epoll_thread_fd) 
{
	int  								desc_ready, rc, cerrar_hilo = FALSE, socket;
	struct epoll_event* 				events;
	struct mensaje_saludo 				saludo;
	struct mensaje_posicion 			posicion;
	struct mensaje_reconocimiento 		reconocimiento;
	struct mensaje_nombre_request 		nombre_request;
	struct mensaje_nombre_reply 		nombre_reply;
	struct mensaje_desconexion			desconexion;
	_tipo_mensaje 						tipo_mensaje;
	vector<_cliente_id>					clientes;
	uint8_t								buffer[200];
	ssize_t 							mensaje_size;

	// Este puntero se rellenará con eventos en la llamada a epoll_wait. Antes de llamarlo
	// hay que alojar memoria para almacenar hasta MAXEVENTS eventos.
	events = (epoll_event*) calloc(MAXEVENTS, sizeof(events));

	do {
		/* epoll_wait devuelve cuantos descriptores hay disponibles para leer y el puntero de
		eventos lo rellena con un vector de eventos con los descriptores y las flags asociadas
		al evento */
		desc_ready = epoll_wait(epoll_thread_fd, events, MAXEVENTS, -1);

		if(desc_ready < 0)
		{
			perror("epoll_wait() error");
		}

		for(int i = 0; i < desc_ready; i++)
		{
			/* Por cada descriptor disponible tenemos una estructura epoll_event lista.
			Lo único que tenemos que hacer es iterar sobre este vector y obtener el
			descriptor de socket asociado a cada evento*/
			socket = events[i].data.fd;

			// Leemos los 8 primeros bits, correspondientes al tipo de mensaje
			rc = recv(socket, &tipo_mensaje, sizeof(uint8_t),0);

			//if(rc < 0)
			//	perror("recv() error");

			if(rc <= 0)
			{
				printf("Se ha desconectado un cliente. Socket: %d\n", socket);
				epoll_ctl(epoll_thread_fd, EPOLL_CTL_DEL, socket, NULL);
				close(socket);
				buffer[0] = MENSAJE_DESCONEXION;
				desconexion.cliente_id_origen = socket;
				memcpy(&buffer[1],&desconexion, sizeof(desconexion));
				for(uint j=0; j < clientes.size(); j++) 
				{
					if(clientes[j]==socket)
					{
						clientes.erase(clientes.begin() + j);
						break;
					}
				}
				for(uint j=0; j < clientes.size(); j++) 
				{
					if (send(clientes[j], buffer, sizeof(_tipo_mensaje) + sizeof(desconexion), 0)< 0)
					{
						perror("send() error ");
						printf("Socket conflictivo: %d\n", clientes[j]);
					}

				}
				
				continue;
			}

			switch (tipo_mensaje)
			{
			case MENSAJE_SALUDO:
				//printf("Mensaje de saludo recibido.\n");
				rc = recv(socket, &saludo, sizeof(saludo), 0);
				if(rc < 0)
				{
					perror("recv() error");
					break;
				}
				
				memcpy(&buffer[0], &tipo_mensaje, sizeof(tipo_mensaje));
				memcpy(&buffer[1], &saludo, sizeof(saludo));
				for(uint j=0; j < clientes.size(); j++) 
				{
					send(clientes[j],buffer, sizeof(saludo) + sizeof(tipo_mensaje), 0);
				}
				clientes.push_back(socket);
				//printf("Se ha conectado %s\n", saludo.nombre);
				break;
			case MENSAJE_POSICION:
				rc = recv(socket, &posicion, sizeof(posicion), 0);
				if(rc < 0)
				{
					perror("recv() error");
					break;
				}
				tipo_mensaje = MENSAJE_POSICION;
				memcpy(&buffer[0], &tipo_mensaje, sizeof(tipo_mensaje));
				memcpy(&buffer[1], &posicion, sizeof(posicion));
				mensaje_size = sizeof(tipo_mensaje) + sizeof(posicion);
				for(uint j=0; j < clientes.size(); j++) 
				{
					if(clientes[j]!=socket)
					{
						send(clientes[j],buffer, mensaje_size, 0);
					}
				}
				break;
			case MENSAJE_RECONOCIMIENTO:
				rc = recv(socket, &reconocimiento, sizeof(reconocimiento), 0);
				if(rc < 0)
				{
					perror("recv() error");
					break;
				}
				tipo_mensaje = MENSAJE_RECONOCIMIENTO;
				memcpy(&buffer[0], &tipo_mensaje, sizeof(tipo_mensaje));
				memcpy(&buffer[1], &reconocimiento, sizeof(reconocimiento));
				mensaje_size = sizeof(tipo_mensaje) + sizeof(reconocimiento);
				send(reconocimiento.cliente_id_destino, buffer, mensaje_size, 0);
				break;
			case MENSAJE_NOMBRE_REQUEST:
				rc = recv(socket, &nombre_request, sizeof(nombre_request), 0);
				if(rc < 0)
				{
					perror("recv() error");
					break;
				}
				tipo_mensaje = MENSAJE_NOMBRE_REQUEST;
				memcpy(&buffer[0], &tipo_mensaje, sizeof(tipo_mensaje));
				memcpy(&buffer[1], &nombre_request, sizeof(nombre_request));
				mensaje_size = sizeof(tipo_mensaje) + sizeof(nombre_request);
				send(nombre_request.cliente_id_destino, buffer, mensaje_size, 0);
				break;
			case MENSAJE_NOMBRE_REPLY:
				rc = recv(socket, &nombre_reply, sizeof(nombre_reply), 0);
				if(rc < 0)
				{
					perror("recv() error");
					break;
				}
				tipo_mensaje = MENSAJE_NOMBRE_REPLY;
				memcpy(&buffer[0], &tipo_mensaje, sizeof(tipo_mensaje));
				memcpy(&buffer[1], &nombre_reply, sizeof(nombre_reply));
				mensaje_size = sizeof(tipo_mensaje) + sizeof(nombre_reply);
				send(nombre_reply.cliente_id_destino, buffer, mensaje_size, 0);
				break;
			default:
				printf("Mensaje no reconocido. Identificador de socket: %d. Mensaje leído: %02X\n", socket, tipo_mensaje);
			}
		}
	} while (!cerrar_hilo);
}

void nueva_conexion_thread (int new_sd)
{


    struct mensaje_conexion 					nuevo_mensaje_conexion;
    struct mensaje_conexion_satisfactoria 		conexion_satisfactoria;
    int 										close_conn = FALSE, rc;
    epoll_event 								event;
    _tipo_mensaje 								tipo_mensaje;

    event.data.fd = new_sd;
	event.events = EPOLLIN;

    /* Primero leemos un mensaje que nos indicará que tipo de mensaje acaba de enviarnos el cliente */
    rc = recv(new_sd, &tipo_mensaje, sizeof(_tipo_mensaje),0);
    if (rc < 0)
    {
        perror("recv() failed");
        close_conn = TRUE;
    }
    
    //printf("\n--------------------------------------------\n");
	//printf("Nueva conexión.\n");
    /* Una vez recibido el tipo de mensaje, vamos a leer a qué grupo desea unirse en caso de ser un mensaje de
    conexión. Usamos un flujo try/catch ya que intentaremos añadir el cliente al set de su grupo. En caso de que
    el grupo no exista, saltará una excepción, donde crearemos el nuevo grupo */
	try
	{
		// Antes que nada comprobamos que es el mensaje que esperamos. Si no, no haremos nada */
		if(tipo_mensaje == MENSAJE_CONEXION)
		{
			//printf("Mensaje de conexión a grupo recibido.\n");

			// En caso de ser el mensaje esperado, esperamos a que nos envíe la estructura del mensaje
			rc = recv(new_sd, &nuevo_mensaje_conexion, sizeof(mensaje_conexion), 0);

   			if (rc < 0)
            {
                perror("recv() failed");
                close_conn = TRUE;
            }

   			if(rc == 0)
   			{
   				close_conn = TRUE;
   			}

   			//printf("Grupo solicitado: %d.\n", nuevo_mensaje_conexion.grupo);

   			// Una vez recibida la estructura del mensaje de conexión, añadimos el cliente al set de su grupo
   			mtx.lock();
   			epoll_ctl(grupos_sets.at(nuevo_mensaje_conexion.grupo), EPOLL_CTL_ADD, new_sd, &event);
   			//printf("Miembro añadido satisfactoriamente a grupo.\n");	
   		}

	} catch (const std::out_of_range& oor) {

		/* En caso de que el grupo al que se desea unir el cliente no exista, debemos crear un nuevo set, que 
		asociaremos al id de grupo nuevo mediante el map. Inicializaremos el set y añadiremos este primer
		cliente. Además añadimos el hilo al vector de hilos de grupos */
		//printf("Grupo no existía. Creando GRUPO %d.\n", nuevo_mensaje_conexion.grupo);
		int new_epoll_fd;
		new_epoll_fd = epoll_create1(0);
		grupos_sets.insert(pair<uint8_t,int>(nuevo_mensaje_conexion.grupo,new_epoll_fd));
		grupos_hilos.push_back(thread(grupo_thread, new_epoll_fd));
		epoll_ctl(new_epoll_fd, EPOLL_CTL_ADD, new_sd, &event);
	}

    if (rc == 0)
    {
       close_conn = TRUE;

    }

    if (close_conn)
    {
        close(new_sd);
    }

    tipo_mensaje = MENSAJE_CONEXION_SATISFACTORIA;
	_tipo_mensaje buffer[50];
	buffer[0] = tipo_mensaje;
	conexion_satisfactoria.cliente_id = new_sd;
	memcpy(&buffer[1], &conexion_satisfactoria, sizeof(mensaje_conexion_satisfactoria));
	send(new_sd, buffer, sizeof(_tipo_mensaje) + sizeof(mensaje_conexion_satisfactoria),0);
	mtx.unlock();
}

/* Función principal del servidor. La función corre sobre el hilo principal, y no realiza ninguna
tarea de reenvío de paquetes. El hilo principal comprobará si hay conexiones entrantes, y esperará
hasta escuchar su mensaje de conexión a grupo, momento en que notificará al hilo del grupo que, a partir
de ahora, también escuche los mensajes de este nuevo cliente. Si no existe el grupo con la ID solicitada,
entonces se creará un nuevo hilo */
int main (int argc, char *argv[])
{
   int    listen_sd, new_sd;
   thread* t;
   UNUSED(t);

   struct sockaddr_in   addr;

   listen_sd = socket(AF_INET, SOCK_STREAM, 0); // Obtenemos el identificador del socket que está a la escucha
   if (listen_sd < 0)
   {
      perror("socket() failed");
      exit(-1);
   }

   memset(&addr, 0, sizeof(addr)); // Creamos la estructura para ligar el socket al puerto de la capa TCP 
   addr.sin_family      = AF_INET;
   addr.sin_addr.s_addr = htonl(INADDR_ANY);
   addr.sin_port        = htons(SERVER_PORT);

   if (bind(listen_sd,(struct sockaddr *)&addr, sizeof(addr)) < 0)
   {
      perror("bind() failed");
      close(listen_sd);
      exit(-1);
   }

   // Finalmente, ponemos el socket a escuchar, con una cola de 256 conexiones 
   if (listen(listen_sd, 256) < 0)					// en espera
   {
      perror("listen() failed");
      close(listen_sd);
      exit(-1);
   }

   int epoll_fd;
   struct epoll_event event;
   struct epoll_event *events;

   epoll_fd = epoll_create1(0);

   event.data.fd = listen_sd;
   event.events = EPOLLIN;

   events = (epoll_event*) calloc (MAXEVENTS, sizeof(event));

   epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sd, &event);

   int epoll_n;

   /* A continuación empieza el bucle principal del servidor, que sólo terminará cuando haya ocurrido algún error.
   Este bucle corre solamente desde el hilo principal, y será desde este donde se realizará la conexión de nuevos
   clientes y se añadirán al set de su grupo correspondiente, o se creará un nuevo hilo en caso de no existir este */
   do
   {
   		epoll_n = epoll_wait(epoll_fd, events, MAXEVENTS, -1);

		for (int i = 0; i < epoll_n; i++)
		{
		    if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (!(events[i].events & EPOLLIN)))
		    {
		        perror("epoll_wait() error");
		        continue;
		    }

		    new_sd = accept(listen_sd, NULL, NULL);
            if (new_sd < 0)
            {
            	perror("accept() failed");
            	continue; // En caso de fallo, cerramos servidor
            }

            /* Cuando una nueva conexión llega al servidor, se lanzará un nuevo hilo que escuchará su mensaje de
            conexión y la añadirá al set de su grupo o creará un grupo nuevo si es necesario. El objetivo de que cada
            conexión nueva tenga su propio hilo es que, si la segunda llamada a recv() se queda bloqueada porque el
            cliente tarda en responder o no envía información válida, el hilo principal del servidor no se quede colgado
            y pueda aceptar otras conexiones mientras tanto */
            t = new thread(nueva_conexion_thread, new_sd); 
		}
    } while (TRUE);

    close(listen_sd);

    return 0;
}