#include "network.h"


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

    int fl = fcntl(listen_sd, F_GETFL);
    fcntl(listen_sd, F_SETFL, fl | O_NONBLOCK);


    if (listen(listen_sd, LISTEN_QUEUE) < 0) {
        perror("listen");
        exit(-1);
    }
    return listen_sd;
}

int async_write(struct epoll_data_client* data, void* buffer, int length)
{
    memcpy(data->write_buffer + data->write_count, buffer, length);
    data->write_count += length;
    return async_write_delay(data);
}

int async_write_delay(struct epoll_data_client* data)
{
    int rc;

    if(data->write_count == 0)
        return 0;

    do
    {
        rc = send(data->socketfd, data->write_buffer, data->write_count, MSG_NOSIGNAL);

        if(rc < 0)
        {
            if( errno == EWOULDBLOCK || errno == EAGAIN)
            {
                return 0;
            }
            //perror("async_write_delay->send()");
            return -1;
        }

        if(rc == 0)
        {
            return 0;
        }

        data->write_count = data->write_count - rc;
        //cout << data->write_count << endl << flush;
        memcpy(data->write_buffer, data->write_buffer + rc, data->write_count);
    } while(rc > 0);

    return rc;
}

int async_read(struct epoll_data_client *data, void *buffer, int length)
{
    int rc;

    do
    {
        rc = read(data->socketfd, data->read_buffer_ptr, data->read_count);

        if(rc < 0)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return READ_BLOCK;
            }

            perror("async_read->read()");

            return READ_ERROR;
        }

        if( rc == 0)
        {
            return READ_CLOSE;
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
#ifdef _DEBUG_
            //printf("Recibido mensaje completo. Tamaño: %d\n", data->read_count_total);
#endif
            memcpy(buffer, data->read_buffer, length);
            data->read_buffer_ptr = data->read_buffer;
            data->tipo_mensaje_read = false;
            data->read_count = 1;
            data->read_count_total = 0;
            return READ_SUCCESS;
        }
    } while(rc > 0);

    return READ_ERROR;
}


void init_epoll_data(int socketfd, struct epoll_data_client * data)
{
    data->socketfd = socketfd;
    data->read_buffer_ptr = data->read_buffer;
    data->tipo_mensaje_read = false;
    data->read_count = 1;
    data->write_count = 0;
    data->grupoid = 0;
}
