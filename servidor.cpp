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
/*	  Además se ha abandonado el uso de la sentencia select() debido a su bajo rendimiento y escala-  */
/*    bilidad. En su lugar utilizamos epoll, un sistema sólo de Linux presente desde la versión del       */
/*    kernel 2.6. epoll no sólo elimina el límite que tenía select() en cuanto a total de descriptores,   */
/*    sino que además el coste de select() era lineal con el número de descriptores a comprobar, mientras */
/*    que el de epoll es lineal. Esto provoca que la aplicación sea mucho más escalable, más eficiente,   */
/*    y soporte más clientes.                                                                             */
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
#include <iostream>

#include "mensajes.h"

#define SERVER_PORT  12345
#define MAXEVENTS	 100000

#define TRUE             1
#define FALSE            0



using namespace std;

/* Almacenaremos todos los hilos creados para los grupos en un vector, para, cuando terminemos, esperar a que
estos hilos terminen antes */
vector<thread> grupos_hilos;


/* Declaramos un mutex global que usará el hilo de conexión cuando tenga que añadir nuevos miembros a los grupos
o crear grupos nuevos */
mutex mutex_conexion;

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

		/* Si epoll tiene un error, devolverá negativo */
		if(desc_ready < 0)
		{
			perror("[GRUPO_HILO] epoll_wait() error");
		}

		/* Si no da error, epoll devuelve el número de descriptores que han disparado los eventos; es decir
		los que están preparados con datos para leer */
		for(int i = 0; i < desc_ready; i++)
		{
			/* Por cada descriptor disponible tenemos una estructura epoll_event lista.
			Lo único que tenemos que hacer es iterar sobre este vector y obtener el
			descriptor de socket asociado a cada evento */
			socket = events[i].data.fd;

			// Leemos los 8 primeros bits, correspondientes al tipo de mensaje
			rc = recv(socket, &tipo_mensaje, sizeof(uint8_t),0);

			/* Tanto en caso de que haya un error leyendo dicho socket, o de que la lectura devuelva cero,
			se procederá a eliminar al cliente del grupo y cerrar la conexión */
			if(rc <= 0)
			{
				// Mostramos mensaje por consola
				printf("[DESCONEXIÓN] Socket: %d, recv:%d\n", socket, rc);

				// Eliminamos el socket para que el hilo ya no compruebe sus mensajes y cerramos conexión
				epoll_ctl(epoll_thread_fd, EPOLL_CTL_DEL, socket, NULL);
				close(socket);

				/* Además, puesto que es el servidor el único que es consciente de estas desconexiones, creamos
				un mensaje de desconexión, indicando quién se ha desconectado, y se lo enviamos a los demás
				miembros del grupo */

				// El primer byte siempre es el tipo de mensaje
				buffer[0] = MENSAJE_DESCONEXION;

				// La ID Origen se refiere al cliente que se ha desconectado
				desconexion.cliente_id_origen = socket;

				// Copiamos la estructura del mensaje a contuniación del byte de tipo de mensaje
				memcpy(&buffer[1],&desconexion, sizeof(desconexion));

				/* Ahora, recorreremos toda la lista de clientes conocidos para eliminar el que se acaba de 
				desconectar */
				for(uint j=0; j < clientes.size(); j++) 
				{
					if(clientes[j]==socket)
					{
						// Antes de reenviar, debemos borrar el cliente desconectado de la lista
						clientes.erase(clientes.begin() + j);
						break;
					}
				}
				for(uint j=0; j < clientes.size(); j++) 
				{
					/* Por cada miembro del grupo, eviamos el byte de tipo de mensaje y el mensaje.
					En clientes[j] se almacena el socket de cada cliente, le enviamos el buffer y un tamaño
					igual a el byte de tipo más la estructura mandada */
					do
					{
						rc = send(clientes[j], buffer, sizeof(_tipo_mensaje) + sizeof(desconexion), 0);
						if (rc < 0)
						{
							perror("[DESCONEXIÓN] send() error ");
							printf("Socket conflictivo: %d\n", clientes[j]);
						}
					} while(rc < 0);

				}
				
				/* Si hemos recibido un mensaje de error al leer el mensaje o el cliente se ha desconectado, miramos
				el siguiente evento de epoll y no hace falta que miremos el tipo de mensaje ya que no hemos leído nada */
				continue;

			} else {

				/* En caso de recibir datos correctamente, miramos qué valor contiene ese primer byte leído, correspondiente
				al tipo de mensaje, y actuamos según sea necesário */
				switch (tipo_mensaje)
				{
				case MENSAJE_SALUDO:
					
					/* Los mensaje de saludo son obligatorios para todos los clientes. Cuando el servidor les consiga unir a
					un grupo, estos deberán mandar un único mensaje de saludo indicando su nombre y su id. */

					// Recibimos el resto del mensaje, la estructura mensaje_saludo
					rc = recv(socket, &saludo, sizeof(saludo), 0);
					if(rc < 0)
					{
						perror("recv() error");
						break;
					}
					
					// Copiamos el tipo de mensaje al primer byte del buffer
					memcpy(&buffer[0], &tipo_mensaje, sizeof(tipo_mensaje));

					// Y el mismo mensaje de saludo a partir del segundo byte
					memcpy(&buffer[1], &saludo, sizeof(saludo));

					/* Enviamos el mensaje de saludo a cada cliente conocido. Este reenvío no es estrictamente necesário
					ya que los propios clientes disponen de mecanismos para averiguar la identidad de miembros del grupo
					de los cuales no han recibido su saludo. Aún así, este sencillo mensaje de saludo ayuda a no saturar la
					red cada vez que se conecta un nuevo cliente */
					for(uint j=0; j < clientes.size(); j++) 
					{
						rc = send(clientes[j],buffer, sizeof(saludo) + sizeof(tipo_mensaje), 0);

						if(rc < 0)
						{
							perror("[SALUDO] send() error");
							break;
						}
					}

					/* Al terminar, añadimos el cliente a nuestra lista. Importante remarcar que no hace falta que añadamos
					el socket a nuestro identificador de epoll, pues esa tarea se delega en el hilo principal que maneja la
					conexión de nuevos clientes */
					clientes.push_back(socket);
					break;

				case MENSAJE_POSICION:
					/* Los mensajes de posición no implican ninguna lógica en el lado del servidor y no hay que procesar
					nada, siendo la única tarea del servidor la de reenviar el paquete a los demás vecinos */

					// Recibimos el resto del mensaje y comprobamos errores
					rc = recv(socket, &posicion, sizeof(posicion), 0);
					if(rc < 0)
					{
						perror("recv() error");
						break;
					}

					// Copiamos el tipo de mensaje al primer byte del buffer
					memcpy(&buffer[0], &tipo_mensaje, sizeof(tipo_mensaje));

					// Copiamos la misma estructura de posición que hemos leído a partir del segundo byte del buffer
					memcpy(&buffer[1], &posicion, sizeof(posicion));

					/* Por cada cliente conocido, reenviamos la información */
					for(uint j=0; j < clientes.size(); j++) 
					{
						if(clientes[j]!=socket)
						{
							rc = send(clientes[j],buffer, sizeof(tipo_mensaje) + sizeof(posicion), 0);

							if(rc < 0)
							{
								perror("[POSICIÓN] send() error");
								break;
							}
						}
					}
					break;

				case MENSAJE_RECONOCIMIENTO:

					/* El mensaje de reconocimiento no es una inundación indiscriminada como el saludo o la posición,
					sino que hay que reenviar el paquete sólo al cliente destino. Gracias a que las ID son las mismas que
					los descriptores usadas por el servidor, tan sólo hay que reenviar el paquete al socket que aparezca
					en el campo de ID destino. */

					// Recibimos la estructura y comprobamos errores
					rc = recv(socket, &reconocimiento, sizeof(reconocimiento), 0);
					if(rc < 0)
					{
						perror("recv() error");
						break;
					}

					// Copiamos el tipo de mensaje leído al primer byte del buffer
					memcpy(&buffer[0], &tipo_mensaje, sizeof(tipo_mensaje));

					// Copiamos la misma estructura de mensaje a partir del segundo byte del buffer
					memcpy(&buffer[1], &reconocimiento, sizeof(reconocimiento));

					// Y simplemente enviamos el paquete de vuelta al campo destino en caso de ser un cliente real
					cout << "Recibido ACK de " << socket << " hacia " << reconocimiento.cliente_id_destino << endl;

					rc = send(reconocimiento.cliente_id_destino, buffer, sizeof(tipo_mensaje) + sizeof(reconocimiento), 0);

					if(rc < 0)
					{
						perror("[RECONOCIMIENTO] send() error");
						break;
					}
					break;

				case MENSAJE_NOMBRE_REQUEST:

					/* Los mensajes de petición de nombre se usan cuando un cliente desconoce la identidad de algún vecino
					y le envía un mensaje para descubrir su nombre y poder mostrarlo en la interfaz y esperar sus ACKs (Lo
					que llamamos por 'conocerlo'). Al igual que los mensajes de reconocimiento, estos mensajes tienen un
					destinatario, y la función del servidor será simplemente reenviarlo a esa ID */

					// Recibimos la estructura y comprobamos errores
					rc = recv(socket, &nombre_request, sizeof(nombre_request), 0);
					if(rc < 0)
					{
						perror("recv() error");
						break;
					}

					// Copiamos el tipo de mensaje leído al primer byte del buffer
					memcpy(&buffer[0], &tipo_mensaje, sizeof(tipo_mensaje));

					// Copiamos el mismo mensaje recibido a partir del segundo byte del buffer
					memcpy(&buffer[1], &nombre_request, sizeof(nombre_request));

					// Enviamos a su destinatario el mensaje completo con su identificador de tipo
					rc = send(nombre_request.cliente_id_destino, buffer, sizeof(tipo_mensaje) + sizeof(nombre_request), 0);

					if(rc < 0)
					{
						perror("[NOMBRE_REQUEST] send() error");
						break;
					}
					break;

				case MENSAJE_NOMBRE_REPLY:

					/* Los mensajes de respuesta de nombre son idénticos para el servidor. La única diferencia reside en
					que estos llevan además un campo de nombre, que usará el cliente para anotarse la identidad de su 
					vecino. Se actuará igual que en el caso de petición de nombre o de mensaje de reconocimiento */

					// Recibimos la estructura y comprobamos errores
					rc = recv(socket, &nombre_reply, sizeof(nombre_reply), 0);
					if(rc < 0)
					{
						perror("recv() error");
						break;
					}

					// Copiamos el tipo de mensaje leído al primer byte del buffer
					memcpy(&buffer[0], &tipo_mensaje, sizeof(tipo_mensaje));

					// Copiamos el mismo mensaje recibido a partir del segundo byte del buffer
					memcpy(&buffer[1], &nombre_reply, sizeof(nombre_reply));
					
					// Devolvemos la respuesta al destino 
					rc = send(nombre_reply.cliente_id_destino, buffer, sizeof(tipo_mensaje) + sizeof(nombre_reply), 0);

					if(rc < 0)
					{
						perror("[NOMBRE_REPLY] send() error");
						break;
					}
					break;
				default:

					/* En caso de no reconocer el tipo de mensaje que nos ha llegado, mostramos un mensaje de error */
					printf("[ERROR] Mensaje no reconocido. Socket: %d. Mensaje: %02X\n", socket, tipo_mensaje);
				}
			}
		}
	} while (!cerrar_hilo);
}

void nueva_conexion_thread (int new_sd)
{
	/* Este es el hilo que se lanzará cada vez que el servidor reciba una nueva conexión. Cuando lo haga, recibirá un
	nuevo descriptor de socket con la llamada a accept(), y se le pasará a esta función para que procese la conexión a
	un grupo del nuevo cliente. Se ha decidido paralelizar la conexión al servidor para no bloquearla cuando algún 
	cliente se quedase a medias entre la conexión TCP y el envío del mensaje de solicitud de grupo */

    struct mensaje_conexion 					nuevo_mensaje_conexion;
    struct mensaje_conexion_satisfactoria 		conexion_satisfactoria;
    int 										close_conn = FALSE, rc;
    epoll_event 								event;
    _tipo_mensaje 								tipo_mensaje;

    /* Rellenamos la estructura event, que luego se le tendrá que pasar a epoll_ctl para que notifique a su grupo
	de los nuevos mensajes del usuario */

	// Relenamos el campo de descriptor de socket
    event.data.fd = new_sd;

    // Y el de tipo de eventos. En este caso, sólo nos interesa cuando el cliente nos haya enviado algo
	event.events = EPOLLIN;

    // A continuación leemos un mensaje que nos indicará que tipo de mensaje acaba de enviarnos el cliente 
    rc = recv(new_sd, &tipo_mensaje, sizeof(_tipo_mensaje),0);
    if (rc < 0)
    {
        perror("recv() failed");
        close_conn = TRUE;
    }

    if (rc == 0)
    {
       close_conn = TRUE;

    }
    
    /* Una vez recibido el tipo de mensaje, vamos a leer a qué grupo desea unirse en caso de ser un mensaje de
    conexión. Usamos un flujo try/catch ya que intentaremos añadir el cliente al set de su grupo. En caso de que
    el grupo no exista, saltará una excepción, donde crearemos el nuevo grupo. */
	try
	{
		// Antes que nada comprobamos que es el mensaje que esperamos. Si no, no haremos nada */
		if(tipo_mensaje != MENSAJE_CONEXION)
		{
			close_conn = TRUE;
		} else {

			// En caso de ser el mensaje esperado, esperamos a que nos envíe la estructura del mensaje
			rc = recv(new_sd, &nuevo_mensaje_conexion, sizeof(mensaje_conexion), 0);

			/* Si hay algún error o se ha cerrado la conexión antes de que recibamos el mensaje de conexión,
			cerramos la conexión con el cliente y cerramos el hilo de ejecución */
   			if (rc < 0)
            {
                perror("recv() failed");
                close_conn = TRUE;
            }

   			if(rc == 0)
   			{
   				close_conn = TRUE;
   			}

   			// Si no hemos recibido ningún error, procesamos la petición
   			if(rc > 0)
   			{
	   			/* Ahora es cuando empieza la sección crítica de la conexión y deberemos hacer un bloqueo. Como ahora
	   			ya hemos leído todos los datos necesários del cliente, no dependemos de él para generar una respuesta y
	   			procesar su petición, así que estamos seguros de que no nos va a bloquear la conexión de otros clientes */
	   			mutex_conexion.lock();

	   			/* Una vez recibida la estructura del mensaje de conexión, añadimos el cliente al set de su grupo. Esto 
	   			es posible gracias a que el cliente indica en su mensaje de conexión a qué grupo desea unirse. En caso de 
	   			que el método map<key,value>.at() no encuentre la clave (Es decir, no exista el grupo), saltará una excepción
	   			std::out_of_range */
	   			epoll_ctl(grupos_sets.at(nuevo_mensaje_conexion.grupo), EPOLL_CTL_ADD, new_sd, &event);	
   			}
   		}

	} catch (const std::out_of_range& oor) {

		/* En caso de que el grupo al que se desea unir el cliente no exista, debemos crear un nuevo set, que 
		asociaremos al id de grupo nuevo mediante el map. Inicializaremos el set y añadiremos este primer
		cliente. Además añadimos el hilo al vector de hilos de grupos */
		int new_epoll_fd;
		new_epoll_fd = epoll_create1(0);

		// Asociamos la nueva ID de grupo con su descriptor epoll
		grupos_sets.insert(pair<uint8_t,int>(nuevo_mensaje_conexion.grupo,new_epoll_fd));

		// Creamos un nuevo hilo y lo insertamos en el vector de hilos
		grupos_hilos.push_back(thread(grupo_thread, new_epoll_fd));

		// Con el grupo creado, registramos los eventos del nuevo cliente
		epoll_ctl(new_epoll_fd, EPOLL_CTL_ADD, new_sd, &event);
	}

	// Si en algún momento hemos obtenido error, cerramos la conexión
    if (close_conn)
    {
        close(new_sd);
    } else {
    	/* En caso que no hayamos encontrado error en ningún sitio, procedemos a enviar de vuelta un mensaje de 
    	conexión satisfactoria, enviándole al cliente su ID y permitiendo que envíe ya su primer mensaje de
    	saludo y, a partir de ahí, empieze los ciclos de mensajes */

    	uint8_t buffer[50];

    	// Construimos el tipo de mensaje a devolver
	    tipo_mensaje = MENSAJE_CONEXION_SATISFACTORIA;
		
		// Copiamos el tipo de mensaje al primer byte del buffer
		buffer[0] = tipo_mensaje;

		/* Rellenamos el campo de ID con el descriptor de socket del cliente en el servidor. La asignación de ID es
		así para agilizar las tareas del servidor en el envío de mensajes a un sólo destinatario, ya que si el usuario
		conoce las IDs de sus vecinos, también conoce los sockets por los que están conectados en el servidor, pudiendo
		enviar esta información en sus paquetes y librando al servidor de la carga de relacionar cada ID con su socket */
		conexion_satisfactoria.cliente_id = new_sd;

		// Copiamos la estructura del mensaje a partir del segundo byte
		memcpy(&buffer[1], &conexion_satisfactoria, sizeof(mensaje_conexion_satisfactoria));

		/* Antes de enviar los datos de vuelta, ya que no vamos a hacer ningún cálculo, desbloqueamos el mútex para
		que otros hilos de conexión puedan trabajar con la información de los grupos */
		mutex_conexion.unlock();

		// Enviamos los datos de vuelta al cliente
		rc = send(new_sd, buffer, sizeof(_tipo_mensaje) + sizeof(mensaje_conexion_satisfactoria),0);

		if (rc < 0)
	    {
	        perror("send() failed");
	        close_conn = TRUE;
	    }
		
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

   // Finalmente, ponemos el socket a escuchar, con una cola de 256 conexiones en espera
   if (listen(listen_sd, 256) < 0)					
   {
      perror("listen() failed");
      close(listen_sd);
      exit(-1);
   }

   int epoll_fd;
   struct epoll_event event;
   struct epoll_event *events;

   /* Creamos un identificador epoll. Como es el hilo principal, la única función va a ser escuchar nuevas
   conexiones, así que solo pondremos a la escucha el socket de escucha */

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

   		// Por cada evento que devuelva epoll, comprobamos que no es un error
		for (int i = 0; i < epoll_n; i++)
		{
		    if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (!(events[i].events & EPOLLIN)))
		    {
		        perror("epoll_wait() error");
		        continue;
		    }

		    /* En caso de haber recibido datos correctamente, llamamos a accept() para que nos devuelva el identificador
		    de socket del nuevo cliente */
		    new_sd = accept(listen_sd, NULL, NULL);

		    // Si da fallo, pasamos al siguiente evento y mostramos el error por consola
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


    // El hilo principal sólo se encarga del socket de escucha, así que su responsabilidad es solamente cerrar ese
    close(listen_sd);

    return 0;
}
