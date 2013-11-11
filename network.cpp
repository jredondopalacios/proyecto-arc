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

/*int async_write(struct epoll_data_client* data, void* buffer, ssize_t length)
{

}*/

int async_read(struct epoll_data_client *data, void *buffer)
{
    int rc;

    do
    {
        rc = read(data->socketfd, data->read_buffer_ptr, data->read_count);
#ifdef _DEBUG_
        printf("Leídos %d bytes en read()\n", rc);
#endif
        if(rc <= 0)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return READ_BLOCK;
            }

            return READ_ERROR;
        }

        data->read_count -= rc;
        data->read_buffer_ptr += rc;
        data->read_count_total += rc;

        if(!data->tipo_mensaje_read)
        {
            data->tipo_mensaje_read = true;
            switch (data->read_buffer[0])
            {
            case MENSAJE_CONEXION:
                data->read_count = sizeof(struct mensaje_conexion);
                break;
            case MENSAJE_SALUDO:
                data->read_count = sizeof(struct mensaje_saludo);
                break;
            case MENSAJE_POSICION:
                data->read_count = sizeof(struct mensaje_posicion);
                break;
            case MENSAJE_RECONOCIMIENTO:
                data->read_count = sizeof(struct mensaje_reconocimiento);
                break;
            case MENSAJE_NOMBRE_REQUEST:
                data->read_count = sizeof(struct mensaje_nombre_request);
                break;
            case MENSAJE_NOMBRE_REPLY:
                data->read_count = sizeof(struct mensaje_nombre_reply);
                break;
            default:
                data->read_count = 1;
                data->tipo_mensaje_read = false;
            }
        }

        if(data->read_count == 0)
        {
            memcpy(buffer, data->read_buffer, data->read_count_total);
            data->read_buffer_ptr = data->read_buffer;
            data->tipo_mensaje_read = false;
            data->read_count = 1;
            data->read_count_total = 0;
            return READ_SUCCESS;
        }
    } while(rc > 0);
}


void init_epoll_data(int socketfd, struct epoll_data_client * data)
{
    data->socketfd = socketfd;
    data->read_buffer_ptr = data->read_buffer;
    data->write_buffer_ptr = data->write_buffer;
    data->tipo_mensaje_read = false;
    data->read_count = 1;
    data->write_count = 0;
}
/*grupoid_t aio_lectura_grupo(int socket) {
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
*/









