#include "network.h"

#define _DEBUG_

using namespace std;

int aio_socket_escucha(int puerto) {
	int listen_sd, optval=1;
    struct sockaddr_in serveraddr;

    if ((listen_sd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        exit(-1);
    }

    if (setsockopt(listen_sd, SOL_SOCKET, SO_REUSEADDR,
                   (const void *)&optval , sizeof(int)) < 0) {
        perror("setsockopt()");
        exit(-1);
    }

    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short) puerto);

    if (bind(listen_sd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        perror("bind");
        exit(-1);
    }
    
    /*if (ioctl(listen_sd, FIONBIO, (char *)&optval) < 0)
    {
        perror("ioctl()");
        close(listen_sd);
        exit(-1);
    }*/

    int fl = fcntl(listen_sd, F_GETFL);
    fcntl(listen_sd, F_SETFL, fl | O_NONBLOCK);


    if (listen(listen_sd, LISTEN_QUEUE) < 0) {
        perror("listen");
        exit(-1);
    }
    return listen_sd;
}

grupoid_t aio_lectura_grupo(int socket) {
	int rc, i = 0;
	char buffer[BUFFER_SIZE], *buffer_ptr;
	struct mensaje_conexion nueva_conexion;
    size_t len = sizeof(struct mensaje_conexion) + sizeof(uint8_t);

	buffer_ptr = buffer;

	while(1)
	{
#ifdef _DEBUG_
        printf("Preparando para recibir mensaje de conexión.\n");
#endif
		rc = read(socket, buffer_ptr + i, len - i);

#ifdef _DEBUG_
        printf("Socket: %d. Bytes recibidos: %d. Esperados: %lu\n", socket, rc, len);
#endif

		if(rc < 0)
		{
			if(errno != EWOULDBLOCK || errno != EAGAIN)
			{
				perror("read()");
                return ERR_SOCKET_READ;
			}

#ifdef _DEBUG_
            printf("No se han recibido suficientes bytes. La siguiente llamada a lectura bloquearía\n");
#endif
			return ERR_BLOCK_READ;
		}

        if(rc == 0)
        {
#ifdef _DEBUG_
            printf("Se ha cerrado la conexión con el cliente.\n");
#endif
            return ERR_CLOSE;
        }

        i += rc; 

        if(i == len)
        {
#ifdef _DEBUG_
            printf("Se han leído los bytes esperados.\n");
#endif
            if(buffer[0] == MENSAJE_CONEXION)
            {
                memcpy(&nueva_conexion, &buffer[1], sizeof(nueva_conexion));
                return nueva_conexion.grupo;
            }
        }		
	}
}










