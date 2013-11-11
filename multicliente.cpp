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

#include "mensajes.h"

using namespace std;

typedef int64_t msec_t;

mutex report_mutex;

msec_t time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (msec_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int cliente_thread(int grupo, string nombre_fichero)
{
	/* Esta función se lanzará en un hilo que representará a un cliente. Sus variables y su comportamiento
	está automatizado y acotado ya que el propósito de este multicliente es servir como cliente de pruebas
	de rendimiento del sistema. Por ello es importante optimizar todas las tareas en este cliente, ya que 
	corremos el riesgo de no poder distinguir el origen de los retrasos, que podría estan causado tanto
	por la latencia de la conexión con el servidor como por lanzar muchos clientes simultáneos en una máquina */

	//ofstream fichero;
    int                     server_socket, rc; 
    uint32_t 				secuencia = 0;
    struct sockaddr_in      dir;
    uint8_t                 buffer[200];
    clienteid_t				cliente_id;
    fd_set					fd, fd_copy;
    bool 					conectado = false;

    struct mensaje_posicion 			posicion;
	struct mensaje_reconocimiento 		reconocimiento;
	struct mensaje_nombre_request 		nombre_request;
	struct mensaje_nombre_reply 		nombre_reply;
	struct mensaje_desconexion 			desconexion;

	// Conectamos con el servidor
	if ((server_socket = socket(PF_INET, SOCK_STREAM, 0))<0)
	{
		perror("socket() error");
		close(server_socket);
		return 0;
	}
    
	dir.sin_family = PF_INET;
	dir.sin_port = htons(12345);
    inet_aton("127.0.0.1",&dir.sin_addr);

	if (connect(server_socket, (struct sockaddr *)&dir, sizeof(struct sockaddr_in))<0)
	{
		perror("connect() error");
		close(server_socket);
		return 0;
	}

	/* Al principio el cliente debe mandar un mensaje solicitando la conexión a un determinado grupo.
	Para ello creamos un buffer donde el primer byte corresponde al tipo de mensaje, y el segundo a la
	estructura del mensaje en sí */
	uint8_t tipo_mensaje;
	struct mensaje_conexion nueva_conexion;

	nueva_conexion.grupo = grupo;
	tipo_mensaje = MENSAJE_CONEXION;

	// Copiamos el tipo al primer byte y la estructura a partir del segundo byte del buffer
	buffer[0] = tipo_mensaje;
	memcpy(&buffer[1], &nueva_conexion, sizeof(nueva_conexion));

	do
	{

		rc = send(server_socket, buffer, sizeof(nueva_conexion) + sizeof(uint8_t),0);
		
		// Si se cierra la conexión o hay un error, cerramos el hilo
		if(rc <= 0)
		{
			perror("[MENSAJE_CONEXION] send() error");
			close(server_socket);
			return 0;
		}



		// Cuando hayamos mandado la solicitud de conexión, esperamos un mensaje confirmandola
		rc = recv(server_socket, buffer, sizeof(uint8_t), 0);

		if(rc <= 0)
		{
			perror("[MENSAJE_CONEXION] recv() error");
			close(server_socket);
			return 0;
		}

		if(buffer[0] == MENSAJE_CONEXION_SATISFACTORIA)
		{
			conectado = true;
		}

		// Mientras no recibamos un mensaje de conexión satisfactoria, seguimos intentando conectar

	}while (!conectado);


	// Cuando tenemos el mensaje de conexión satisfactoria esperado, leemos su contenido
	struct mensaje_conexion_satisfactoria conexion_satisfactoria;
	rc = recv(server_socket, &conexion_satisfactoria, sizeof(mensaje_conexion_satisfactoria), 0);

	// Leemos la ID que nos han asignado y la guardamos
	cliente_id = conexion_satisfactoria.cliente_id;

	// Una vez conectados al grupo, es hora de enviar el mensaje de saludo
	struct mensaje_saludo nuevo_saludo;
	string s = "Jordi";
	tipo_mensaje = MENSAJE_SALUDO;
	nuevo_saludo.cliente_id_origen = cliente_id;

	/* Copiamos el tipo de mensaje al primer byte del buffer, pasamos la cadena a tipo C y copiamos la
	estructura a continuación del byte de tipo */
	buffer[0] = tipo_mensaje;
	strcpy(nuevo_saludo.nombre, s.c_str());
	memcpy(&buffer[1], &nuevo_saludo, sizeof(nuevo_saludo));

	rc = send(server_socket, buffer, sizeof(nuevo_saludo) + sizeof(uint8_t),0);

	if(rc <= 0)
	{
		perror("send() error");
		close(server_socket);
		return 0;
	}

	/*
	stringstream ssm;

	ssm << "client-log-" << cliente_id << ".txt";

	fichero.open(nombre_fichero);
	*/

	struct mensaje_posicion miPosicion;
	miPosicion.cliente_id_origen = cliente_id;
	miPosicion.posicion_x = 100;
	miPosicion.posicion_y = 150;
	miPosicion.posicion_z = -200;

	int64_t ticker,final,inicio = time_ms();

	/* El cliente sólo escucha mensajes del servidor. Al escuchar un sólo socket, la diferencia entre epoll y select
	 no es notable, así que elegimos select por ser un código más portable y reutilizable en otros sistemas */
	FD_ZERO(&fd);
	FD_SET(server_socket, &fd);

	int n;
	struct timeval  timeout;
	timeout.tv_sec = 1;
    timeout.tv_usec = 1000;
    bool nuevo_ciclo = true;

    /* La estructura cliente_info guardará la información de sus vecinos conocidos con la información necesária
    para mostrarlos por la interfaz */
    struct cliente_info {
    	char nombre[NOMBRE_MAX_CHAR];
    	int16_t posicion_x;
		int16_t posicion_y;
		int16_t posicion_z;
    };

    /* La información la mantenemos duplicada en memoria. Una copia sirve para la representación de los datos
    mientras que otra es una copia de apoyo que sirve para comprobar qué clientes nos han devuelto mensajes
    de reconocimiento. Como vamos a realizar muchas búsquedas por ID, nos interesa una función hash para encontrar
    rápidamente a los clientes. Un contenedor map<key,value> ordena los valores por su llave, teniendo un coste de 
    búsqueda menor que si buscásemos secuencialmente por un vector */
    map<int, cliente_info> clientes_conocidos, clientes_copia;

    //fichero << cliente_id << endl;
    ticker = time_ms();

    report_mutex.lock();
    cout << "Empezando HiloID: " << cliente_id << endl;
    report_mutex.unlock();
    // Bucle principal del cliente
	while(true)
	{
		// Primero ejecutamos las dos instrucciones necesarias para el select
		memcpy(&fd_copy, &fd, sizeof(fd));
		n = select(server_socket + 1, &fd_copy, NULL, NULL, &timeout);

		/* La variable nuevo_ciclo estará activada cuando toque hacer un nuevo ciclo. Esto es, cuando tengamos todos
		los mensajes de reconocimiento del ciclo actual */
		if(nuevo_ciclo)
		{
			// report_mutex.lock();
			// cout << "[ID" << cliente_id << "] Empezando Ciclo N." << secuencia << endl;
			// report_mutex.unlock();

			//if(secuencia+1 == 3000)
			//	break;
			//fichero << "Enviando posición con número de secuencia: " << secuencia << endl;;

			// Copiamos al primer bit del buffer el número del tipo de mensaje de posición
			buffer[0] = MENSAJE_POSICION;

			// Actualizamos la estructura del mensaje con el siguiente número de secuencia
			miPosicion.numero_secuencia = ++secuencia;

			// Copiamos la estructura ya actualizada a continuación del byte de tipo de mensaje
			memcpy(&buffer[1], &miPosicion, sizeof(miPosicion));
			rc = send(server_socket, buffer, sizeof(miPosicion) + sizeof(mensaje_t), 0);

			if(rc <= 0)
			{
				perror("[MENSAJE_POSICION] send() error");
				close(server_socket);
				return 0;
			}

			// Reiniciamos el contador de tiempo para saber cuanto tiempo durará el siguiente ciclo
			ticker = time_ms();

			// Copiamos los clientes conocidos, aquellos de los que deberemos esperar sus ACKs
			clientes_copia = clientes_conocidos;
			nuevo_ciclo = false;

			// report_mutex.lock();
			// cout << "[ID" << cliente_id << "] Esperando " << clientes_copia.size() << " mensajes de reconocimiento" << endl;
			// report_mutex.unlock();

			if(clientes_copia.size() == 0)
			{
				nuevo_ciclo = true;
			}
			/*fichero << "Se necesitan encontrar " << clientes_copia.size() << " ACKs coincidentes para siguiente ciclo." << endl;
			if(clientes_copia.empty() && (secuencia > 70))
				break;*/
		} else {
			//cout << "Soy el ID: " << cliente_id << " y me faltan " << clientes_copia.size() << " ACKs." << endl;
		}


		/* Si hemos recibido algún mensaje, y como sólo nos interesan los que nos haya enviado el servidor, no hará falta
		iterar sobre n pues podemos directamente comprobar si es el socket del servidor el que tiene datos para leer */
		if(n > 0)
		{
			if(FD_ISSET(server_socket, &fd_copy))
			{
				mensaje_t tipo;

				// Recibimos el primer byte, que indica el tipo de mensaje recibido
				rc = recv(server_socket, &tipo, sizeof(tipo), 0);

				if(rc <= 0)
				{
					perror("[TIPO_MENSAJE] recv() error");
					close(server_socket);
					return 0;
				}

				switch(tipo)
				{
					case MENSAJE_POSICION:
						{
						/* En caso de recibir un mensaje de posición son varias las tareas que se deben hacer.
						En primer lugar, y sin importar si conocemos o no al cliente, le mandamos de vuelta tan
						pronto como podamos un mensaje de reconocimiento */


						// Recibimos el resto del mensaje y lo guardamos en la estructura correspondiente
						rc = recv(server_socket, &posicion, sizeof(posicion), 0);

						if(rc <= 0)
						{
							perror("[MENSAJE_POSICION] recv() error");
							close(server_socket);
							return 0;
						}

						// report_mutex.lock();
						// cout << "[ID" << cliente_id << "] POSICIÓN. ID: " << posicion.cliente_id_origen << 
						// 											". SECUENCIA: " << posicion.numero_secuencia << endl;
						// report_mutex.unlock();

						// Empezamos a construir el mensaje de reconocimiento con el byte de tipo
						buffer[0] = MENSAJE_RECONOCIMIENTO;

						// El origen del reconocimiento será nuestra propia ID
						reconocimiento.cliente_id_origen = cliente_id;

						// El destino del reconocimiento erá el origen del mensaje de posición. Con su misma secuencia
						reconocimiento.cliente_id_destino = posicion.cliente_id_origen;
						reconocimiento.numero_secuencia = posicion.numero_secuencia;

						// Copiamos los datos del mensaje al buffer
						memcpy(&buffer[1], &reconocimiento, sizeof(reconocimiento));

						//fichero << "Recibida actualización de posición. Enviando reconocimiento a ID " << posicion.cliente_id_origen << endl;
						//report_mutex.lock();
						//cout << "HiloID: " << cliente_id << ". Enviando ACK a Dest: " << reconocimiento.cliente_id_destino << endl;
						//report_mutex.unlock();
						//cout << "[ID" << cliente_id << "] MENSAJE_POSICION de " << posicion.cliente_id_origen << ".\n";
						// report_mutex.lock();
						// cout << "[ID" << cliente_id << "] ENVÍO DE ACK. ID_DEST: " << reconocimiento.cliente_id_destino << 
						// 										". SECUENCIA: " << reconocimiento.numero_secuencia << endl;
						// report_mutex.unlock();
						rc = send(server_socket, buffer, sizeof(reconocimiento) + sizeof(mensaje_t), 0);

						if(rc <= 0)
						{
							perror("[MENSAJE_RECONOCIMIENTO] send() error");
							close(server_socket);
							return 0;
						}

						/* A la hora de actualizar los datos, intentamos acceder a su clave en el contendor y 
						actualizar su información con los nuevos valores. Si no se encuentra la clave, se lanza una
						excepción y se pasa a enviar un mensaje de petición de información */
						try 
						{

							// Actualizamos posición
							clientes_conocidos.at(posicion.cliente_id_origen).posicion_x = posicion.posicion_x;
							clientes_conocidos.at(posicion.cliente_id_origen).posicion_y = posicion.posicion_y;
							clientes_conocidos.at(posicion.cliente_id_origen).posicion_z = posicion.posicion_z;
						} catch (const std::out_of_range& oor) {

							/*// En caso de fallo, construímos mensaje de petición de información
							buffer[0] = MENSAJE_NOMBRE_REQUEST;

							// Indicamos quienes somos y de quién queremos la información
							nombre_request.cliente_id_origen = cliente_id;
							nombre_request.cliente_id_destino = posicion.cliente_id_origen;

							// Copiamos la información en el buffer
							memcpy(&buffer[1],&nombre_request, sizeof(nombre_request));
							//fichero << "Enviando petición de información a ID " << posicion.cliente_id_origen << endl;

							// Y mandamos el mensaje al servidor
							//rc = send(server_socket, buffer, sizeof(nombre_request) + sizeof(mensaje_t), 0);

							if(rc <= 0)
							{
								perror("send() error");
								close(server_socket);
								return 0;
							}*/
						}

						/*
						for(uint j=0; j < clientes_conocidos.size(); j++)
						{
							if(clientes_conocidos[j].id == posicion.cliente_id_origen)
							{
								//fichero << "Cliente encontrando. Atualizando información." << endl;
								encontrado = true;
								clientes_conocidos[j].posicion_x = posicion.posicion_x;
								clientes_conocidos[j].posicion_y = posicion.posicion_y;
								clientes_conocidos[j].posicion_z = posicion.posicion_z;
								break;
							}
						}
						if(!encontrado){
							buffer[0] = MENSAJE_NOMBRE_REQUEST;
							nombre_request.cliente_id_origen = cliente_id;
							nombre_request.cliente_id_destino = posicion.cliente_id_origen;
							memcpy(&buffer[1],&nombre_request, sizeof(nombre_request));
							//fichero << "Enviando petición de información a ID " << posicion.cliente_id_origen << endl;
							send(sock, buffer, sizeof(nombre_request) + sizeof(mensaje_t), 0);
						}*/
						break;
						}
					case MENSAJE_RECONOCIMIENTO:
						{
						/* En caso de recibir un mensaje de reconocimiento hay que hacer una doble comprobación.
						Tanto el número de secuencia como la ID de origen deben de ser los esperados. En caso contrario
						descartamos el mensaje. Si sí que coincide, eliminamos el cliente de la lista de reconocimientos
						pendientes */

						// Leemos el resto del mensaje
						rc = recv(server_socket, &reconocimiento, sizeof(reconocimiento), 0);

						if(rc <= 0)
						{
							perror("[MENSAJE_RECONOCIMIENTO] recv() error");
							close(server_socket);
							return 0;
						}

						// report_mutex.lock();
						// cout << "[ID" << cliente_id << "] RECIBO DE RECONOCIMIENTO. ID_ORIG: " << reconocimiento.cliente_id_origen <<
						// 									". SECUENCIA: " << reconocimiento.numero_secuencia << endl;
						// report_mutex.unlock();

						// Comprobamos que corresponde al último mensaje de posición enviado
						if(reconocimiento.numero_secuencia == secuencia)
						{
							/* find() devuelve un iterador apuntando al final si no se encuentra la clave. Por tanto
							buscaremos si existe el ID del reconocimiento en la lista de reconocimientos esperados. */
							if (clientes_copia.find(reconocimiento.cliente_id_origen) != clientes_copia.end())
							{
								// Elimiamos el cliente de la lista de reconocimientos esperados
								clientes_copia.erase(reconocimiento.cliente_id_origen);
								// report_mutex.lock();
								// cout << "[ID" << cliente_id << "] ACK ESPERADO. QUEDAN " << clientes_copia.size() << endl;
								// report_mutex.unlock();
							}
							/*for(uint j=0; j < clientes_copia.size(); j++)
							{
								//fichero << "Se ha encontrado un ACK. Buscando coincidencias...";
								if(clientes_copia[j].id == reconocimiento.cliente_id_origen)
								{
									clientes_copia.erase(clientes_copia.begin() + j);
									fichero << "ACK de cliente en espera encontrado." << endl;
									fichero << "Aún espero " << clientes_copia.size() << " mensajes más." << endl;
									break;
								}
							}*/
						}

						// Si no hay que esperar ningún mensaje de reconocimiento más, empezaremos nuevo ciclo
						if(clientes_copia.empty())
						{
							/*fichero << " >>>>>>>>>>>>>>> Recibidos todos los ACK. Latencia de ciclo: " << time_ms() - ticker << endl;
							fichero << "Empezando nuevo ciclo..." << endl;
							//nuevo_ciclo = true;
							buffer[0] = MENSAJE_POSICION;
							miPosicion.numero_secuencia = ++secuencia;
							memcpy(&buffer[1], &miPosicion, sizeof(miPosicion));
							send(sock, buffer, sizeof(miPosicion) + sizeof(mensaje_t), 0);
							ticker = time_ms();*/
							clientes_copia = clientes_conocidos;
							nuevo_ciclo = true;
						}


						if(clientes_conocidos.find(reconocimiento.cliente_id_origen) == clientes_conocidos.end())
						{
						// En caso de no encontrarlo, construímos mensaje de petición de información
							buffer[0] = MENSAJE_NOMBRE_REQUEST;

							// Indicamos quienes somos y de quién queremos la información
							nombre_request.cliente_id_origen = cliente_id;
							nombre_request.cliente_id_destino = reconocimiento.cliente_id_origen;

							// Copiamos la información en el buffer
							memcpy(&buffer[1],&nombre_request, sizeof(nombre_request));
							//fichero << "Enviando petición de información a ID " << posicion.cliente_id_origen << endl;

							// Y mandamos el mensaje al servidor

							rc = send(server_socket, buffer, sizeof(nombre_request) + sizeof(mensaje_t), 0);

							if(rc <= 0)
							{
								perror("[MENSAJE_NOMBRE_REQUEST] send() error");
								close(server_socket);
								return 0;
							}
						}
						break;
						}
					case MENSAJE_SALUDO:
						{
						/* En el caso del mensaje de saludo, puesto que este mensaje se envia siempre antes que cualquier
						otro, sabemos seguro que no lo tenemos en la lista de clientes conocidos. Lo añadimos */
						rc = recv(server_socket, &nuevo_saludo, sizeof(mensaje_saludo), 0);

						if(rc <= 0)
						{
							perror("[MENSAJE_SALUDO] recv() error");
							close(server_socket);
							return 0;
						}

						struct cliente_info nuevo_cliente;
						
						// Inicializamos la información del nuevo cliente
						strcpy(nuevo_cliente.nombre, nuevo_saludo.nombre);
						nuevo_cliente.posicion_x = 0;
						nuevo_cliente.posicion_y = 0;
						nuevo_cliente.posicion_z = 0;

						// Insertamos el nuevo cliente en nuestro contenedor
						int id_aux = nuevo_saludo.cliente_id_origen;
						clientes_conocidos.insert(pair<int,cliente_info>(id_aux, nuevo_cliente));
						cout << "Se ha conectado un nuevo miembro a GRUPO" << endl;
						//fichero << ">>> Conozco " << clientes_conocidos.size() << " clientes <<<" << endl;
						break;
						}
					case MENSAJE_NOMBRE_REQUEST:
						{
						/* Si recibimos un mensaje de petición de nombre, simplemente enviamos el mensaje de vuelta
						añadiendo nuestro nombre */
						rc = recv(server_socket, &nombre_request, sizeof(nombre_request),0);

						if(rc <= 0)
						{
							perror("[MENSAJE_NOMBRE_REQUEST] recv() error");
							close(server_socket);
							return 0;
						}

						// Empezamos construyendo el mensaje copiando el byte de tipo al buffer
						buffer[0] = MENSAJE_NOMBRE_REPLY;

						// Construimos el mensaje con nuestra propia ID y la ID de quién ha pedido la información
						nombre_reply.cliente_id_origen = cliente_id;
						nombre_reply.cliente_id_destino = nombre_request.cliente_id_origen;

						// Copiamos nuestro nombre a la estructura
						strcpy(nombre_reply.nombre, s.c_str());

						// Copiamos todos los datos al buffer y enviamos de vuelta al servidor
						memcpy(&buffer[1], &nombre_reply, sizeof(nombre_reply));
						rc = send(server_socket, buffer, sizeof(uint8_t) + sizeof(nombre_reply), 0);

						if(rc <= 0)
						{
							perror("[MENSAJE_NOMBRE_REPLY] send() error");
							close(server_socket);
							return 0;
						}
						//fichero << "Cliente ID " << nombre_request.cliente_id_origen << " solicita información." << endl;
						break;
						}
					case MENSAJE_NOMBRE_REPLY:
						{
						/* Un mensaje de respuesta de informacón sólo lo recibiremos si hemos enviado antes un mensaje de
						petición de información; y esta sólo la haremos si no conociamos a un cliente que nos ha enviado
						un mensaje de reconocimiento. Como no hay otras vías por las que el cliente nos haya mandado su
						información, podemos estar seguros de añadir su información a memoria sin tener duplicados */

						// Leemos el resto del mensaje para saber de quién hemos recibido respuesta
						rc = recv(server_socket, &nombre_reply, sizeof(nombre_reply), 0);

						if(rc <= 0)
						{
							perror("[MENSAJE_NOMBRE_REPLY] recv() error");
							close(server_socket);
							return 0;
						}

						struct cliente_info reply_info;

						// Copiamos el nombre del nuevo cliente e inicializamos sus datos
						strcpy(reply_info.nombre, nombre_reply.nombre);
						reply_info.posicion_x = 0;
						reply_info.posicion_y = 0;
						reply_info.posicion_z = 0;

						// Añadimos el nuevo cliente al contenedor
						int id_aux = nombre_reply.cliente_id_origen;
						if(clientes_conocidos.find(id_aux) == clientes_conocidos.end())
						{
							clientes_conocidos.insert(pair<int,cliente_info>(id_aux, reply_info));
						}

						/*if(clientes_conocidos.size() > 9)
						{
							report_mutex.lock();
							cout << "[ID" << cliente_id << " ERROR] El número de clientes conocidos es de " << clientes_conocidos.size() << endl;
							report_mutex.unlock();
						}*/
						/*for(uint j=0; j < clientes_conocidos.size(); j++)
						{
							if(clientes_conocidos[j].id == nombre_reply.cliente_id_origen)
							{
								//fichero << "Se ha recibido información de un cliente conocido." << endl;
								encontrado = true;
								break;
							}
						}						
						if(!encontrado)
						{
							clientes_conocidos.push_back(info);
							fichero << "Recibida información de ID: " << info.id << endl;
							fichero << ">>> Conozco " << clientes_conocidos.size() << " clientes <<<" << endl;
						}*/
						
						break;
						}
					case MENSAJE_DESCONEXION:
						{
						/* En el caso de desconexión, tenemos que eliminar, en caso de que exista, el cliente tanto
						de la lista de clientes conocidos como de la lista temporal de espera de reconocimientos */
						rc = recv(server_socket, &desconexion, sizeof(desconexion), 0);

						if(rc <= 0)
						{
							perror("[MENSAJE_DESCONEXION] recv() error");
							close(server_socket);
							return 0;
						}

						//fichero << "Mensaje de desconexión." << endl;


						// Buscamos el cliente desconectado en el contendor de la copia
						map<int, cliente_info>::iterator busqueda = clientes_copia.find(desconexion.cliente_id_origen);

						// Si lo encontramos en la copia, nos aseguramos que también está en el original. Borramos ambos
						if(busqueda != clientes_copia.end())
						{
							/* Si está en el contenedor copia, lo eliminamos de ambos y terminamos la sentencia case
							del switch */
							clientes_copia.erase(busqueda);
							clientes_conocidos.erase(desconexion.cliente_id_origen);
							// report_mutex.lock();
							// cout << "[ID" << cliente_id << "] DESCONEXION. ID: " << desconexion.cliente_id_origen << 
							// 							". QUEDAN " << clientes_copia.size() << " MENSAJES ACK." << endl;
							// report_mutex.unlock();
						}

						// Si no está en la copia, buscamos en el original
						busqueda = clientes_conocidos.find(desconexion.cliente_id_origen);

						//Si está en el original, borramos
						if(busqueda != clientes_conocidos.end())
						{
							// report_mutex.lock();
							// cout << "[ID" << cliente_id << "] DESCONEXION. ID: " << desconexion.cliente_id_origen << endl;
							// report_mutex.unlock();
							clientes_conocidos.erase(busqueda);
						}

						

						/*for(uint j=0; j < clientes_conocidos.size(); j++)
						{
							if(clientes_conocidos[j].id == desconexion.cliente_id_origen)
							{
								fichero << "Se ha desconectado un cliente conocido." << endl;
								clientes_conocidos.erase(clientes_conocidos.begin() + j);
								break;
							}
						}
						for(uint j=0; j < clientes_copia.size(); j++)
						{
							if(clientes_copia[j].id == desconexion.cliente_id_origen)
							{
								clientes_copia.erase(clientes_copia.begin() + j);

								fichero << "Se ha desconectado un cliente del que se esperaba su reconocimiento." << endl;
								break;
							}
						}*/
						//fichero << "Aún espero " << clientes_copia.size() << " mensajes de reconocimiento más." << endl;

						// Aparte, si este usuario desconectado era el último que esperábamos, empezamos un nuevo ciclo
						if(clientes_copia.empty())
						{
							nuevo_ciclo = true;
							/*buffer[0] = MENSAJE_POSICION;
							miPosicion.numero_secuencia = ++secuencia;
							memcpy(&buffer[1], &miPosicion, sizeof(miPosicion));
							send(sock, buffer, sizeof(miPosicion) + sizeof(mensaje_t), 0);
							ticker = time_ms();*/
							clientes_copia = clientes_conocidos;
						}
						break;
						}
					default:
						// report_mutex.lock();
						// cout << "[ID" << cliente_id << " ERROR] Mensaje no reconocido." << endl;
						// report_mutex.unlock();
						break;
				}
			}
		}
	}
	//fichero << "Completados 100 ciclos, tiempo total " << (time_ms() - inicio)  / 1000 << " segundos." << endl;
	//fichero << "Tiempo medio de ciclo: " << ((time_ms() - inicio)  / (1000 * (secuencia + 1.0))) << " segundos." << endl;
	//fichero.close();

	// Apuntamos el tiempo total en que hemos realizado todos los ciclos
	final = time_ms();
	// Bloqueamos la ejecución para evitar que se solape el texto en la salida estándar
	report_mutex.lock();
	cout << "El Hilo con ID " << cliente_id << " ha terminado." << endl;
	cout << "Tiempo medio por ciclo: " << ((final - inicio)  / (1000 * (secuencia + 1.0))) << " segundos." << endl;
	report_mutex.unlock();

	close(server_socket);

	return 0;
}

int main(int argc, const char* argv[])
{
	vector<thread> hilos;

	int grupos = atoi(argv[1]);
	int clientes_en_grupo = atoi(argv[2]);

	for(int i = 0; i < grupos; i++)
	{
		for(int j = 0; j < clientes_en_grupo; j++)
		{
			stringstream ssm;
			ssm << "client-log-" << i << "-" << j << ".txt";
			hilos.push_back(thread(cliente_thread, i, ssm.str()));
		}
	}
	cout << "A la espera de que terminen los hilos..." << endl;
	for(uint i = 0; i < hilos.size(); i++)
	{
		hilos[i].join();
		//cout << "Hilo " << i << " terminado." << endl;
	}
	cout << "Prueba finalizada." << endl;
	return 0;
}

