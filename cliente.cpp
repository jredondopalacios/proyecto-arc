#include <iostream>
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
#include <unistd.h>
#include <string>
#include <arpa/inet.h>
#include <ctime>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include "mensajes.h"

using namespace std;

typedef int64_t msec_t;

msec_t time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (msec_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int main(int argc, const char * argv[])
{

    int                     sock, rc; 
    uint32_t secuencia = 0;
    struct sockaddr_in      dir;
    uint8_t                 buffer[200];
    _cliente_id				cliente_id;
    fd_set					fd, fd_copy;

    struct mensaje_posicion 			posicion;
	struct mensaje_reconocimiento 		reconocimiento;
	struct mensaje_nombre_request 		nombre_request;
	struct mensaje_nombre_reply 		nombre_reply;
	struct mensaje_desconexion 			desconexion;

	UNUSED(nombre_reply);
	UNUSED(nombre_request);

	if ((sock=socket(PF_INET, SOCK_STREAM, 0))<0)
	{
		perror("socket() error");
		exit(0);
	}
    
	dir.sin_family=PF_INET;
	dir.sin_port=htons(12345);
    inet_aton(argv[1],&dir.sin_addr);

	if (connect(sock, (struct sockaddr *)&dir, sizeof(struct sockaddr_in))<0)
	{
		perror("connect() error");
		exit(0);
	}

	uint8_t tipo_mensaje = MENSAJE_CONEXION;
	buffer[0] = tipo_mensaje;

	struct mensaje_conexion nueva_conexion;
	nueva_conexion.grupo = 3;

	memcpy(&buffer[1], &nueva_conexion, sizeof(nueva_conexion));

	rc = send(sock, buffer, sizeof(nueva_conexion) + sizeof(uint8_t),0);
	
	if(rc < 0)
	{
		perror("send() error");
		exit(0);
	}

	rc = recv(sock, buffer, sizeof(uint8_t) + sizeof(mensaje_conexion_satisfactoria), 0);



	printf("Recibidos datos de confirmación del servidor.\n");


	cliente_id = buffer[1];
	printf("Mi ID de cliente es: %d\n", cliente_id);
	struct mensaje_saludo nuevo_saludo;
	string s = "Jordi";
	strcpy(nuevo_saludo.nombre, s.c_str());
	tipo_mensaje = MENSAJE_SALUDO;
	buffer[0] = tipo_mensaje;
	memcpy(&buffer[1], &nuevo_saludo, sizeof(nuevo_saludo));

	rc = send(sock, buffer, sizeof(nuevo_saludo) + sizeof(uint8_t),0);
	
	if(rc < 0)
	{
		perror("send() error");
		exit(0);
	}

	struct mensaje_posicion miPosicion;
	miPosicion.cliente_id_origen = cliente_id;
	miPosicion.posicion_x = 100;
	miPosicion.posicion_y = 150;
	miPosicion.posicion_z = -200;

	int64_t ticker = 0;

	FD_ZERO(&fd);
	FD_SET(sock, &fd);
	int n;
	struct timeval  timeout;
	timeout.tv_sec = 0;
    timeout.tv_usec = 1000;

    struct cliente_info {
    	_cliente_id id;
    	char nombre[NOMBRE_MAX_CHAR];
    	int16_t posicion_x;
		int16_t posicion_y;
		int16_t posicion_z;
    };

    vector<cliente_info> clientes_conocidos, clientes_copia;

	while(1)
	{
		memcpy(&fd_copy, &fd, sizeof(fd));
		n = select(sock + 1, &fd_copy, NULL, NULL, &timeout);

		if((time_ms() - ticker) > 1000)
		{
			buffer[0] = MENSAJE_POSICION;
			miPosicion.numero_secuencia = ++secuencia;
			memcpy(&buffer[1], &miPosicion, sizeof(miPosicion));
			send(sock, buffer, sizeof(miPosicion) + sizeof(_tipo_mensaje), 0);
			ticker = time_ms();
			clientes_copia = clientes_conocidos;
		}

		if(n > 0)
		{
			if(FD_ISSET(sock, &fd_copy))
			{
				_tipo_mensaje tipo;
				recv(sock, &tipo, sizeof(tipo), 0);
				bool encontrado;
				switch(tipo)
				{
					case MENSAJE_POSICION:
						printf("Recibido mensaje de posición.\n");
						recv(sock, &posicion, sizeof(posicion), 0);
						printf("Origen ID: %d\n", posicion.cliente_id_origen);
						buffer[0] = MENSAJE_RECONOCIMIENTO;
						reconocimiento.cliente_id_origen = cliente_id;
						reconocimiento.cliente_id_destino = posicion.cliente_id_origen;
						reconocimiento.numero_secuencia = posicion.numero_secuencia;
						memcpy(&buffer[1], &reconocimiento, sizeof(reconocimiento));
						send(sock, buffer, sizeof(reconocimiento) + sizeof(_tipo_mensaje), 0);
						encontrado = false;
						for(uint j=0; j < clientes_conocidos.size(); j++)
						{
							if(clientes_conocidos[j].id == posicion.cliente_id_origen)
							{
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
							send(sock, buffer, sizeof(nombre_request) + sizeof(_tipo_mensaje), 0);
						}
						break;
					case MENSAJE_RECONOCIMIENTO:
						printf("Recibido mensaje de reconocimiento.\n");
						recv(sock, &reconocimiento, sizeof(reconocimiento), 0);
						encontrado = false;
						if(reconocimiento.numero_secuencia == secuencia)
						{
							for(uint j=0; j < clientes_copia.size(); j++)
							{
								if(clientes_copia[j].id == reconocimiento.cliente_id_origen)
								{
									encontrado = true;
									clientes_copia.erase(clientes_copia.begin() + j);
									printf("He encontrado un ACK esperado.\n");
									break;
								}
							}
						}
						if(!encontrado){
							buffer[0] = MENSAJE_NOMBRE_REQUEST;
							nombre_request.cliente_id_origen = cliente_id;
							nombre_request.cliente_id_destino = reconocimiento.cliente_id_origen;
							memcpy(&buffer[1],&nombre_request, sizeof(nombre_request));
							send(sock, buffer, sizeof(nombre_request) + sizeof(_tipo_mensaje), 0);
						}
						if(clientes_copia.empty())
						{
							printf("Me han llegado todos los ACKs esperados.\n");
						}
						break;
					case MENSAJE_SALUDO:
						printf("Se ha conectado un nuevo miembro.\n");
						break;
					case MENSAJE_NOMBRE_REQUEST:
						recv(sock, &nombre_request, sizeof(nombre_request),0);
						buffer[0] = MENSAJE_NOMBRE_REPLY;
						nombre_reply.cliente_id_origen = cliente_id;
						nombre_reply.cliente_id_destino = nombre_request.cliente_id_origen;
						strcpy(nombre_reply.nombre, s.c_str());
						memcpy(&buffer[1], &nombre_reply, sizeof(nombre_reply));
						send(sock, buffer, sizeof(uint8_t) + sizeof(nombre_reply), 0);
						break;
					case MENSAJE_NOMBRE_REPLY:
						recv(sock, &nombre_reply, sizeof(nombre_reply), 0);
						struct cliente_info info;
						info.id = nombre_reply.cliente_id_origen;
						strcpy(info.nombre, nombre_reply.nombre);
						clientes_conocidos.push_back(info);
						break;
					case MENSAJE_DESCONEXION:
						recv(sock, &desconexion, sizeof(desconexion), 0);
						for(uint j=0; j < clientes_conocidos.size(); j++)
						{
							if(clientes_conocidos[j].id == desconexion.cliente_id_origen)
							{
								clientes_conocidos.erase(clientes_conocidos.begin() + j);
								break;
							}
						}
						break;
					default:
						break;
				}
			}
		}
	}

	return 0;
}
