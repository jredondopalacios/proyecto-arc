#ifndef _MENSAJES_H_
#define _MENSAJES_H_

#define MENSAJE_DESCONEXION					8
#define MENSAJE_CONEXION 					1
#define MENSAJE_CONEXION_SATISFACTORIA		2
#define MENSAJE_SALUDO   					3
#define MENSAJE_POSICION					4
#define MENSAJE_RECONOCIMIENTO				5
#define MENSAJE_NOMBRE_REQUEST				6
#define MENSAJE_NOMBRE_REPLY				7

#define NOMBRE_MAX_CHAR						20

#define UNUSED(expr) do { (void)(expr); } while (0)

typedef int 			clienteid_t;
typedef int16_t    		grupoid_t;
typedef uint8_t  		mensaje_t;

struct mensaje_desconexion {
	clienteid_t cliente_id_origen;
};

struct mensaje_conexion {
	grupoid_t grupo;
} __attribute__((packed));

struct mensaje_conexion_satisfactoria {
	clienteid_t cliente_id;
} __attribute__((packed));

struct mensaje_saludo {
	clienteid_t cliente_id_origen;
	char nombre[NOMBRE_MAX_CHAR];
} __attribute__((packed));

struct mensaje_posicion {
	clienteid_t cliente_id_origen;
	int16_t posicion_x;
	int16_t posicion_y;
	int16_t posicion_z;
	uint32_t numero_secuencia;
} __attribute__((packed));

struct mensaje_reconocimiento {
	clienteid_t cliente_id_origen;
	clienteid_t cliente_id_destino;
	uint32_t numero_secuencia;
} __attribute__((packed));

struct mensaje_nombre_request {
	clienteid_t cliente_id_origen;
	clienteid_t cliente_id_destino;
} __attribute__((packed));

struct mensaje_nombre_reply {
	clienteid_t cliente_id_origen;
	clienteid_t cliente_id_destino;
	char nombre[NOMBRE_MAX_CHAR];
} __attribute__((packed));

#endif