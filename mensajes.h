#define MENSAJE_DESCONEXION		0
#define MENSAJE_CONEXION 		1
#define MENSAJE_SALUDO   		2

struct mensaje_conexion {
	uint8_t grupo;
};

struct mensaje_saludo {
	char nombre[20];
};