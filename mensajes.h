#define MENSAJE_CONEXION 		0
#define MENSAJE_SALUDO   		1

struct mensaje_conexion {
	uint8_t grupo;
};

struct mensaje_saludo {
	char* nombre[20];
};