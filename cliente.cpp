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


#include "mensajes.h"

using namespace std;

int main(int argc, const char * argv[])
{

    int                     sock, rc;
    struct sockaddr_in      dir;
    uint8_t                 buffer[200];

	if ((sock=socket(PF_INET, SOCK_STREAM, 0))<0)
	{
		perror("socket() error");
		exit(0);
	}
    
	dir.sin_family=PF_INET;
	dir.sin_port=htons(12345);
    inet_aton("192.168.1.139",&dir.sin_addr);

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

	rc = recv(sock, buffer, sizeof(uint8_t), 0);

	printf("Recibidos datos de confirmación del servidor.\n");

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

	sleep(10);

	return 0;
}
