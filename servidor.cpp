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

#define UNUSED(expr) do { (void)(expr); } while (0)

#define TRUE             1
#define FALSE            0

using namespace std;

typedef int socket_t;
typedef int cliente_id;

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

inline int recv_tipo_mensaje(int sd) {
	uint8_t tipo;
	int rc;
	rc = recv(sd, &tipo, sizeof(tipo),0);
	if (rc < 0)
	{
		return rc;
	}
	return tipo;
}

/* grupo_thread es la función que ejecutará cada hilo. Cada hilo se le asgina a un grupo y
es el único encargado de pasar todos los mensajes de los miembros del grupo, de esta forma
sólo el hilo es consciente de lo que pasa en cada grupo así de sus miembros conectados, 
aislando cada hilo las tareas de gestión de mensajes de los grupos. Es la principal herramienta
para paralelizar las tareas del servidor. */
void grupo_thread (fd_set* thread_set) 
{
	int  desc_ready, rc, cerrar_hilo = FALSE, max_sd = 0, contador_ids = 0;
	map<cliente_id,socket_t> clientes;
	fd_set working_set;
	uint8_t tipo_mensaje;
	struct mensaje_saludo saludo;

	UNUSED(contador_ids);
	UNUSED(clientes);

	printf("Grupo creado!\n");

	struct timeval  timeout;
	timeout.tv_sec  = 0;    // Declaramos un timeout para select() de un milisegundo
    timeout.tv_usec = 1000;

	do {
		memcpy(&working_set, thread_set, sizeof(*thread_set));

		rc = select(max_sd + 1, &working_set, NULL, NULL, &timeout);

		if(rc < 0)
		{
			perror("select() error");
			return;
		}

		desc_ready = rc;

		for(int i = 0; i <= max_sd; i++)
		{
			if(FD_ISSET(i,&working_set))
			{
				desc_ready -= 1;

				switch (recv_tipo_mensaje(i))
				{
				case MENSAJE_SALUDO:
					printf("Mensaje de saludo recibido.\n");
					rc = recv(i, &saludo, sizeof(saludo), 0);
					if(rc < 0)
					{
						perror("recv() error");
						break;
					}
					printf("Se ha conectado %s\n", saludo.nombre);
					break;
				default:
					printf("Mensaje no reconocido. Identificador de socket: %d\n", i);
				}
			}
		}
	} while (!cerrar_hilo);
}

void nueva_conexion_thread (int new_sd)
{
	printf("Nueva conexión.\n");

    struct mensaje_conexion nuevo_mensaje_conexion;
    int close_conn = FALSE, rc;

    /* Primero leemos un mensaje que nos indicará que tipo de mensaje acaba de enviarnos el cliente */
    rc = recv_tipo_mensaje(new_sd);
    if (rc < 0)
    {
        perror("recv() failed");
        close_conn = TRUE;
    }

    /* Una vez recibido el tipo de mensaje, vamos a leer a qué grupo desea unirse en caso de ser un mensaje de
    conexión. Usamos un flujo try/catch ya que intentaremos añadir el cliente al set de su grupo. En caso de que
    el grupo no exista, saltará una excepción, donde crearemos el nuevo grupo */
		try
		{
			// Antes que nada comprobamos que es el mensaje que esperamos. Si no, no haremos nada */
			if(rc == MENSAJE_CONEXION)
			{
				printf("Mensaje de conexión a grupo recibido.\n");

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

	   			printf("Grupo solicitado: %d.\n", nuevo_mensaje_conexion.grupo);

	   			// Una vez recibida la estructura del mensaje de conexión, añadimos el cliente al set de su grupo
	   			FD_SET(new_sd,grupos_sets.at(nuevo_mensaje_conexion.grupo));			}

		} catch (const std::out_of_range& oor) {

			/* En caso de que el grupo al que se desea unir el cliente no exista, debemos crear un nuevo set, que 
			asociaremos al id de grupo nuevo mediante el map. Inicializaremos el set y añadiremos este primer
			cliente. Además añadimos el hilo al vector de hilos de grupos */
			printf("Grupo no existía. Creando grupo.\n");
			fd_set new_set;
			FD_ZERO(&new_set);
			FD_SET(new_sd, &new_set);
			grupos_sets.insert(pair<uint8_t,fd_set*>(nuevo_mensaje_conexion.grupo,&new_set));
			grupos_hilos.push_back(thread(grupo_thread, &new_set));
		}

    if (rc == 0)
    {
       close_conn = TRUE;

    }

    if (close_conn)
    {
        close(new_sd);
    }
}

/* Función principal del servidor. La función corre sobre el hilo principal, y no realiza ninguna
tarea de reenvío de paquetes. El hilo principal comprobará si hay conexiones entrantes, y esperará
hasta escuchar su mensaje de conexión a grupo, momento en que notificará al hilo del grupo que, a partir
de ahora, también escuche los mensajes de este nuevo cliente. Si no existe el grupo con la ID solicitada,
entonces se creará un nuevo hilo */
int main (int argc, char *argv[])
{
   int    listen_sd, new_sd;
   int    end_server = FALSE;
   thread* t;
   UNUSED(t);

   struct sockaddr_in   addr;
   struct timeval       timeout;
   fd_set        master_set, working_set;

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

   FD_ZERO(&master_set);   // Inicializamos el masters_set con el set de escucha. Esto es imprescindible para
   						   // despues hacer la llamada al select() y escuchar nuevas conexiones
   FD_SET(listen_sd, &master_set);

   timeout.tv_sec  = 0;    // Declaramos un timeout para select() de un milisegundo
   timeout.tv_usec = 1000;

   /* A continuación empieza el bucle principal del servidor, que sólo terminará cuando haya ocurrido algún error.
   Este bucle corre solamente desde el hilo principal, y será desde este donde se realizará la conexión de nuevos
   clientes y se añadirán al set de su grupo correspondiente, o se creará un nuevo hilo en caso de no existir este */
   do
   {
   		/* Copiamos el set principal en otro set temporal. Eso se hace porque select() modifica el set pasado por
   		parámetros, y si dejamos que modifique el set maestro, el sistema no funcionaría. Cada llamada a select() debe
   		contar con una copia nueva del set maestro */
        memcpy(&working_set, &master_set, sizeof(master_set));

        if (select(listen_sd + 1, &working_set, NULL, NULL, &timeout) < 0)
        {
        	// Si select() devuelve cero, ha habido un error, y terminamos el bucle principal del servidor
            break;
        }

    	/* Como solo estamos escuchando nuevas conexiones, sólo se necesita comprobar el socket de escucha */
        if (FD_ISSET(listen_sd, &working_set))
        {

        	/* La llamada a accept() devuelve el identificador de socket del nuevo cliente. Antes de 
        	hacer nada con ella, comprobamos que es positivo. En caso contrario se trata de un error */
            new_sd = accept(listen_sd, NULL, NULL);
            if (new_sd < 0)
            {
            	perror("accept() failed");
            	break; // En caso de fallo, cerramos servidor
            }

            /* Cuando una nueva conexión llega al servidor, se lanzará un nuevo hilo que escuchará su mensaje de
            conexión y la añadirá al set de su grupo o creará un grupo nuevo si es necesario. El objetivo de que cada
            conexión nueva tenga su propio hilo es que, si la segunda llamada a recv() se queda bloqueada porque el
            cliente tarda en responder o no envía información válida, el hilo principal del servidor no se quede colgado
            y pueda aceptar otras conexiones mientras tanto */
            t = new thread(nueva_conexion_thread, new_sd); 
     	}

    } while (end_server == FALSE);

    close(listen_sd);

    return 0;
}
