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
/**********************************************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <errno.h>
#include <thread>
#include <map>
#include <vector>
#include <string.h>
#include <unistd.h>

#include "mensajes.h"

#define SERVER_PORT  12345

#define TRUE             1
#define FALSE            0

using namespace std;

/* max_sd es la unica variable global, ya que se usará por todos los hilos para
saber cual es el máximo identificador de socket al iterar sobre la llamada a
 select().   */
int max_sd;

/* grupo_thread es la función que ejecutará cada hilo. Cada hilo se le asgina a un grupo y
es el único encargado de pasar todos los mensajes de los miembros del grupo, de esta forma
sólo el hilo es consciente de lo que pasa en cada grupo así de sus miembros conectados, 
aislando cada hilo las tareas de gestión de mensajes de los grupos. Es la principal herramienta
para paralelizar las tareas del servidor. */
void grupo_thread (fd_set* thread_set) {
	//int contador_ids;
	//map<int,int> clientes;
	//fd_set master_set, working_set;
	printf("Grupo creado!\n");
}

/* Función principal del servidor. La función corre sobre el hilo principal, y no realiza ninguna
tarea de reenvío de paquetes. El hilo principal comprobará si hay conexiones entrantes, y esperará
hasta escuchar su mensaje de conexión a grupo, momento en que notificará al hilo del grupo que, a partir
de ahora, también escuche los mensajes de este nuevo cliente. Si no existe el grupo con la ID solicitada,
entonces se creará un nuevo hilo */
int main (int argc, char *argv[])
{
   int    i, rc;
   int    listen_sd, new_sd;
   int    desc_ready, end_server = FALSE;
   int    close_conn;

   struct sockaddr_in   addr;
   struct timeval       timeout;
   fd_set        master_set, working_set;

   struct mensaje_conexion nuevo_mensaje_conexion;
   uint8_t tipo_mensaje;

   /* Almacenaremos todos los hilos creados para los grupos en un vector, para, cuando terminemos, esperar a que
   estos hilos terminen antes */
   vector<thread> grupos_hilos;

   /* Los fd_set de cada grupo serán los que permitirán al hilo principal notificar de nuevas conexiones a los grupos,
   ya que actualizando cualquiera de estos con FD_SET() provocamos que estos hilos sean notificados en sus bucles select()
   del momento cuando les lleguen nuevos mensajes del cliente que se ha conectado. Se ha optado por un contendor de tipo
   map<key,value> por su coste de búsqueda de log(n). Aún no siendo una operación crítica, sí que va a haber más accesos a
   las estructuras fd_set que inserciones de nuevos grupos, por lo que interesa mantenerlo en un contendeor ordenado y con
   índice binario para búsqueda */
   map<uint8_t,fd_set*> grupos_sets;

   listen_sd = socket(AF_INET, SOCK_STREAM, 0);
   if (listen_sd < 0)
   {
      perror("socket() failed");
      exit(-1);
   }

   memset(&addr, 0, sizeof(addr));
   addr.sin_family      = AF_INET;
   addr.sin_addr.s_addr = htonl(INADDR_ANY);
   addr.sin_port        = htons(SERVER_PORT);
   rc = bind(listen_sd,
             (struct sockaddr *)&addr, sizeof(addr));
   if (rc < 0)
   {
      perror("bind() failed");
      close(listen_sd);
      exit(-1);
   }

   rc = listen(listen_sd, 256);
   if (rc < 0)
   {
      perror("listen() failed");
      close(listen_sd);
      exit(-1);
   }

   FD_ZERO(&master_set);
   max_sd = listen_sd;
   FD_SET(listen_sd, &master_set);

   timeout.tv_sec  = 0;
   timeout.tv_usec = 1000;

   do
   {
        memcpy(&working_set, &master_set, sizeof(master_set));
        rc = select(max_sd + 1, &working_set, NULL, NULL, &timeout);

        if (rc < 0)
        {
           break;
        }

        desc_ready = rc;

        for (i=0; i <= max_sd  &&  desc_ready > 0; ++i)
        {
        if (FD_ISSET(i, &working_set))
        {
            desc_ready -= 1;

            if (i == listen_sd)
            {
                new_sd = accept(listen_sd, NULL, NULL);
                if (new_sd < 0)
                {
                	perror("accept() failed");
                	end_server = TRUE;
                }

                printf("Nueva conexión.\n");

                FD_SET(new_sd, &master_set);
                if (new_sd > max_sd)
                	max_sd = new_sd;
            } else {

               close_conn = FALSE;

                rc = recv(i, &tipo_mensaje, sizeof(tipo_mensaje), 0);
                if (rc < 0)
                {
                    perror("recv() failed");
                    close_conn = TRUE;
                }

       			try
           		{
           			if(tipo_mensaje == MENSAJE_CONEXION)
           			{
           				printf("Mensaje de conexión a grupo recibido.\n");

           				rc = recv(i, &nuevo_mensaje_conexion, sizeof(mensaje_conexion), 0);

	           			if (rc < 0)
		                {
	                        perror("recv() failed");
	                        close_conn = TRUE;
		                }

	           			if(rc == 0)
	           			{
	           				close_conn = TRUE;
	           			}

	           			FD_SET(i,grupos_sets.at(nuevo_mensaje_conexion.grupo));
	           			FD_CLR(i, &master_set);
           			}

           		} catch (const std::out_of_range& oor) {

           			printf("Grupo no existía. Creando grupo.\n");
           			fd_set new_set;
           			FD_ZERO(&new_set);
           			FD_SET(i, &new_set);
           			grupos_sets.insert(pair<uint8_t,fd_set*>(nuevo_mensaje_conexion.grupo,&new_set));
           			grupos_hilos.push_back(thread(grupo_thread, &new_set));
           		}

                if (rc == 0)
                {
                   close_conn = TRUE;

                }

                if (close_conn)
                {
                    close(i);
                    FD_CLR(i, &master_set);
                    if (i == max_sd)
                    {
                        while (FD_ISSET(max_sd, &master_set) == FALSE)
                        max_sd -= 1;
                    }
                }
            }
         }
      } 

   } while (end_server == FALSE);

   for (i=0; i <= max_sd; ++i)
   {
      if (FD_ISSET(i, &master_set))
         close(i);
   }

   return 0;
}
