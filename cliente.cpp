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

	if ((sock=socket(PF_INET, SOCK_STREAM, 0))<0)
	{
		perror("socket() error");
		exit(0);
	}
    
	dir.sin_family=PF_INET;
	dir.sin_port=htons(12345);
    inet_aton("127.0.0.1",&dir.sin_addr);

	if (connect(sock, (struct sockaddr *)&dir, sizeof(struct sockaddr_in))<0)
	{
		perror("connect() error");
		exit(0);
	}

	uint8_t tipo_mensaje = MENSAJE_CONEXION;
	rc = send(sock,&tipo_mensaje, sizeof(tipo_mensaje), 0);

	if(rc < 0)
	{
		perror("send() error");
		exit(0);
	}

	struct mensaje_conexion nueva_conexion;
	nueva_conexion.grupo = 3;

	rc = send(sock, &nueva_conexion, sizeof(nueva_conexion),0);
	
	if(rc < 0)
	{
		perror("send() error");
		exit(0);
	}

	return 0;
}