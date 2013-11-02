#define MENSAJE_DESCONEXION					0
#define MENSAJE_CONEXION 					1
#define MENSAJE_CONEXION_SATISFACTORIA		2
#define MENSAJE_SALUDO   					3
#define MENSAJE_POSICION					4
#define MENSAJE_RECONOCIMIENTO				5
#define MENSAJE_NOMBRE_REQUEST				6
#define MENSAJE_NOMBRE_REPLY				7

#define NOMBRE_MAX_CHAR						20

typedef int 			_cliente_id;
typedef uint8_t  		_grupo_id;
typedef uint8_t  		_tipo_mensaje;

struct mensaje_conexion {
	_grupo_id grupo;
} __attribute__((packed));

struct mensaje_conexion_satisfactoria {
	_cliente_id cliente_id;
} __attribute__((packed));

struct mensaje_saludo {
	_cliente_id cliente_id_origen;
	char nombre[NOMBRE_MAX_CHAR];
} __attribute__((packed));

struct mensaje_posicion {
	_cliente_id cliente_id_origen;
	int16_t posicion_x;
	int16_t posicion_y;
	int16_t posicion_z;
	uint32_t numero_secuencia;
} __attribute__((packed));

struct mensaje_reconocimiento {
	_cliente_id cliente_id_origen;
	_cliente_id cliente_id_destino;
	uint32_t numero_secuencia;
} __attribute__((packed));

struct mensaje_nombre_request {
	_cliente_id cliente_id_origen;
	_cliente_id cliente_id_destino;
} __attribute__((packed));

struct mensaje_nombre_reply {
	_cliente_id cliente_id_origen;
	_cliente_id cliente_id_destino;
	char nombre[NOMBRE_MAX_CHAR];
} __attribute__((packed));