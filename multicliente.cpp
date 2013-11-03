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

#include "mensajes.h"

using namespace std;

typedef int64_t msec_t;

mutex mtx;

int contador_hilos = 0;

msec_t time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (msec_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int cliente_thread(int grupo, string nombre_fichero)
{
	ofstream fichero;
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
    inet_aton("192.168.1.137",&dir.sin_addr);

	if (connect(sock, (struct sockaddr *)&dir, sizeof(struct sockaddr_in))<0)
	{
		perror("connect() error");
		exit(0);
	}

	uint8_t tipo_mensaje = MENSAJE_CONEXION;
	buffer[0] = tipo_mensaje;

	struct mensaje_conexion nueva_conexion;
	nueva_conexion.grupo = grupo;

	memcpy(&buffer[1], &nueva_conexion, sizeof(nueva_conexion));

	rc = send(sock, buffer, sizeof(nueva_conexion) + sizeof(uint8_t),0);
	
	if(rc < 0)
	{
		perror("send() error");
		exit(0);
	}

	rc = recv(sock, buffer, sizeof(uint8_t) + sizeof(mensaje_conexion_satisfactoria), 0);





	cliente_id = buffer[1];
	struct mensaje_saludo nuevo_saludo;
	string s = "Jordi";
	strcpy(nuevo_saludo.nombre, s.c_str());
	tipo_mensaje = MENSAJE_SALUDO;
	buffer[0] = tipo_mensaje;
	nuevo_saludo.cliente_id_origen = cliente_id;
	memcpy(&buffer[1], &nuevo_saludo, sizeof(nuevo_saludo));

	rc = send(sock, buffer, sizeof(nuevo_saludo) + sizeof(uint8_t),0);
	stringstream ssm;

	ssm << "client-log-" << cliente_id << ".txt";

	fichero.open(nombre_fichero);
	
	
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

	int64_t ticker = 0, inicio = time_ms();
	FD_ZERO(&fd);
	FD_SET(sock, &fd);
	int n;
	struct timeval  timeout;
	timeout.tv_sec = 1;
    timeout.tv_usec = 1000;
    bool nuevo_ciclo = true;

    struct cliente_info {
    	_cliente_id id;
    	char nombre[NOMBRE_MAX_CHAR];
    	int16_t posicion_x;
		int16_t posicion_y;
		int16_t posicion_z;
    };

    vector<cliente_info> clientes_conocidos, clientes_copia;

    cout << "Cliente conectado a GRUPO " << grupo << " con ID " << cliente_id << endl;
    fichero << cliente_id << endl;

	while(true)
	{
		memcpy(&fd_copy, &fd, sizeof(fd));
		n = select(sock + 1, &fd_copy, NULL, NULL, &timeout);

		if(nuevo_ciclo)
		{
			if(secuencia+1 == 100)
				break;
			fichero << "Enviando posición con número de secuencia: " << secuencia << endl;;
			buffer[0] = MENSAJE_POSICION;
			miPosicion.numero_secuencia = ++secuencia;
			memcpy(&buffer[1], &miPosicion, sizeof(miPosicion));
			send(sock, buffer, sizeof(miPosicion) + sizeof(_tipo_mensaje), 0);
			ticker = time_ms();
			clientes_copia = clientes_conocidos;
			nuevo_ciclo = false;
			fichero << "Se necesitan encontrar " << clientes_copia.size() << " ACKs coincidentes para siguiente ciclo." << endl;
			if(clientes_copia.empty() && (secuencia > 70))
				break;
		}

		if(n > 0)
		{
			if(FD_ISSET(sock, &fd_copy))
			{
				_tipo_mensaje tipo;
				recv(sock, &tipo, sizeof(tipo), 0);
				bool encontrado;
				struct cliente_info info;
				switch(tipo)
				{
					case MENSAJE_POSICION:
						recv(sock, &posicion, sizeof(posicion), 0);
						buffer[0] = MENSAJE_RECONOCIMIENTO;
						reconocimiento.cliente_id_origen = cliente_id;
						reconocimiento.cliente_id_destino = posicion.cliente_id_origen;
						reconocimiento.numero_secuencia = posicion.numero_secuencia;
						memcpy(&buffer[1], &reconocimiento, sizeof(reconocimiento));
						fichero << "Recibida actualización de posición. Enviando reconocimiento a ID " << posicion.cliente_id_origen << endl;
						send(sock, buffer, sizeof(reconocimiento) + sizeof(_tipo_mensaje), 0);
						encontrado = false;
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
							send(sock, buffer, sizeof(nombre_request) + sizeof(_tipo_mensaje), 0);
						}
						break;
					case MENSAJE_RECONOCIMIENTO:
						recv(sock, &reconocimiento, sizeof(reconocimiento), 0);
						if(reconocimiento.numero_secuencia == secuencia)
						{
							for(uint j=0; j < clientes_copia.size(); j++)
							{
								//fichero << "Se ha encontrado un ACK. Buscando coincidencias...";
								if(clientes_copia[j].id == reconocimiento.cliente_id_origen)
								{
									clientes_copia.erase(clientes_copia.begin() + j);
									fichero << "ACK de cliente en espera encontrado." << endl;
									fichero << "Aún espero " << clientes_copia.size() << " mensajes más." << endl;
									break;
								}
							}
						}
						if(clientes_copia.empty())
						{
							/*fichero << " >>>>>>>>>>>>>>> Recibidos todos los ACK. Latencia de ciclo: " << time_ms() - ticker << endl;
							fichero << "Empezando nuevo ciclo..." << endl;
							//nuevo_ciclo = true;
							buffer[0] = MENSAJE_POSICION;
							miPosicion.numero_secuencia = ++secuencia;
							memcpy(&buffer[1], &miPosicion, sizeof(miPosicion));
							send(sock, buffer, sizeof(miPosicion) + sizeof(_tipo_mensaje), 0);
							ticker = time_ms();
							clientes_copia = clientes_conocidos;*/
							nuevo_ciclo = true;
						}
						break;
					case MENSAJE_SALUDO:
						recv(sock, &nuevo_saludo, sizeof(mensaje_saludo), 0);
						info.id = nuevo_saludo.cliente_id_origen;
						strcpy(info.nombre, nuevo_saludo.nombre);
						clientes_conocidos.push_back(info);
						fichero << "Se ha conectado un nuevo miembro a GRUPO: " << grupo << " con ID: " << info.id << endl;
						fichero << ">>> Conozco " << clientes_conocidos.size() << " clientes <<<" << endl;
						break;
					case MENSAJE_NOMBRE_REQUEST:
						recv(sock, &nombre_request, sizeof(nombre_request),0);
						buffer[0] = MENSAJE_NOMBRE_REPLY;
						nombre_reply.cliente_id_origen = cliente_id;
						nombre_reply.cliente_id_destino = nombre_request.cliente_id_origen;
						strcpy(nombre_reply.nombre, s.c_str());
						memcpy(&buffer[1], &nombre_reply, sizeof(nombre_reply));
						send(sock, buffer, sizeof(uint8_t) + sizeof(nombre_reply), 0);
						//fichero << "Cliente ID " << nombre_request.cliente_id_origen << " solicita información." << endl;
						break;
					case MENSAJE_NOMBRE_REPLY:
						recv(sock, &nombre_reply, sizeof(nombre_reply), 0);
						info.id = nombre_reply.cliente_id_origen;
						strcpy(info.nombre, nombre_reply.nombre);
						encontrado = false;
						for(uint j=0; j < clientes_conocidos.size(); j++)
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
						}
						
						break;
					case MENSAJE_DESCONEXION:
						recv(sock, &desconexion, sizeof(desconexion), 0);
						fichero << "Mensaje de desconexión." << endl;
						for(uint j=0; j < clientes_conocidos.size(); j++)
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
						}
						fichero << "Aún espero " << clientes_copia.size() << " mensajes de reconocimiento más." << endl;
						if(clientes_copia.empty())
						{
							nuevo_ciclo = true;
							/*buffer[0] = MENSAJE_POSICION;
							miPosicion.numero_secuencia = ++secuencia;
							memcpy(&buffer[1], &miPosicion, sizeof(miPosicion));
							send(sock, buffer, sizeof(miPosicion) + sizeof(_tipo_mensaje), 0);
							ticker = time_ms();
							clientes_copia = clientes_conocidos;*/
						}
						break;
					default:
						break;
				}
			}
		}
	}
	fichero << "Completados 100 ciclos, tiempo total " << (time_ms() - inicio)  / 1000 << " segundos." << endl;
	fichero << "Tiempo medio de ciclo: " << ((time_ms() - inicio)  / (1000 * (secuencia + 1.0))) << " segundos." << endl;
	fichero.close();
	mtx.lock();
	contador_hilos++;
	cout << "El Hilo " << contador_hilos << " con ID " << cliente_id << "ha terminado." << endl;
	close(sock);
	mtx.unlock();
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