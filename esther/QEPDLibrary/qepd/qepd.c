#include "qepd.h"
#include <string.h>
#include <errno.h>

void conectar(int* servidor,char* IP,int PUERTO) {
	struct sockaddr_in direccionServidor;
	direccionServidor.sin_family = AF_INET;
	direccionServidor.sin_addr.s_addr = inet_addr( (char*) IP);
	direccionServidor.sin_port = htons(PUERTO);
	*servidor = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(*servidor, (struct sockaddr *) &direccionServidor,sizeof(direccionServidor)) < 0) {
		close(*servidor);
		logear_error("No se pudo conectar al servidor",true);
	}
	logear_info("Conectado al servidor");
}
void configurar(char* quienSoy) {

	if (existe_archivo(RUTA_CONFIG)) {
		config = config_create(RUTA_CONFIG);
		int longitud = strlen(quienSoy)+strlen(".log ");
		char *ruta = malloc(longitud+1);
		snprintf(ruta,longitud,"%s%s",quienSoy,".log");
		logger = log_create(ruta, quienSoy, false, LOG_LEVEL_INFO);
		free(ruta);

	}else{
		logear_error("No existe el archivo de configuración",true);
	}

	if(config_keys_amount(config) > 0) {
		establecer_configuracion();
	} else {
		logear_error("Error al leer archivo de configuración",true);
	}
	config_destroy(config);
}
void enviar_header(int socket, char operacion, int bytes) {
	headerDeLosRipeados headerDeMiMensaje;
	headerDeMiMensaje.bytesDePayload = bytes;
	headerDeMiMensaje.codigoDeOperacion = operacion;
	void* header = &headerDeMiMensaje;
	int headerSize = sizeof(headerDeMiMensaje);
	send(socket, header, headerSize, 0);
}
int existe_archivo(const char *ruta) {
    FILE *archivo = fopen(ruta, "r+");

    if (archivo == NULL) {
    	if (errno == EISDIR) {
    		logear_error("Error: \"%s\" es un directorio", false, ruta);
    	}
		return false;
    }

    fclose(archivo);
    return true;
}
void handshake(int socket, char operacion) {
	logear_info("Enviando saludo al servidor");

	enviar_header(socket,operacion,0);

	char* respuesta = malloc(32);
	int bytesRecibidos = recv(socket, respuesta, 32, 0);

	if (bytesRecibidos > 0) {
		logear_info("Saludo recibido: \"%s\"", respuesta);
	}
	else {
		logear_error("Ripeaste papu",true);
	}

	free(respuesta);
}
void logear_error(char* formato, int terminar , ...) {
	va_list args;
	va_start(args, terminar);
	char* mensaje = malloc(512);
	vsnprintf(mensaje,512,formato,args);
	log_error(logger,mensaje);
	vprintf(formato,args);printf("\n");
	va_end(args);
	free(mensaje);
	if (terminar) exit(0);
}
void logear_info(char* formato, ...) {
	va_list args;
	va_start(args, formato);
	char* mensaje = malloc(512);
	vsnprintf(mensaje,512,formato,args);
	log_info(logger,mensaje);
	vprintf(formato,args);printf("\n");
	va_end(args);
	free(mensaje);
}
int recibir_header(int socket, headerDeLosRipeados *header) {
	int bytesARecibir = sizeof(headerDeLosRipeados);
	int bytesRecibidos = recv(socket, header, bytesARecibir, 0);

	return bytesRecibidos;
}
