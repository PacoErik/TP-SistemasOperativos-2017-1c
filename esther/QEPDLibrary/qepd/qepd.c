#include "qepd.h"
#include <string.h>

void conectar(int* servidor,char* IP,int PUERTO) {
	struct sockaddr_in direccionServidor;
	direccionServidor.sin_family = AF_INET;
	direccionServidor.sin_addr.s_addr = inet_addr( (char*) IP);
	direccionServidor.sin_port = htons(PUERTO);
	*servidor = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(*servidor, (struct sockaddr *) &direccionServidor,sizeof(direccionServidor)) < 0) {
		close(*servidor);
		logearError("No se pudo conectar al servidor",true);
	}
	logearInfo("Conectado al servidor");
}
void configurar(char* quienSoy) {

	if (existeArchivo(RUTA_CONFIG)) {
		config = config_create(RUTA_CONFIG);
		int longitud = strlen(quienSoy)+strlen(".log ");
		char *ruta = malloc(longitud+1);
		snprintf(ruta,longitud,"%s%s",quienSoy,".log");
		logger = log_create(ruta, quienSoy, false, LOG_LEVEL_INFO);
		free(ruta);

	}else{
		logearError("No existe el archivo de configuración",true);
	}

	if(config_keys_amount(config) > 0) {
		establecerConfiguracion();
	} else {
		logearError("Error al leer archivo de configuración",true);
	}
	config_destroy(config);
}
void deserializarHeader(headerDeLosRipeados *header, void *buffer) {
	short *pBytesDePayload = (short*) buffer;
	header->bytesDePayload = *pBytesDePayload;
	char *pCodigoDeOperacion = (char*)(pBytesDePayload + 1);
	header->codigoDeOperacion = *pCodigoDeOperacion;
}
void enviarHeader(int socket, char operacion, int bytes) {
	headerDeLosRipeados headerDeMiMensaje;
	headerDeMiMensaje.bytesDePayload = bytes;
	headerDeMiMensaje.codigoDeOperacion = operacion;

	int headerSize = sizeof(headerDeMiMensaje);
	void *headerComprimido = malloc(headerSize);
	serializarHeader(&headerDeMiMensaje, headerComprimido);

	send(socket, headerComprimido, headerSize, 0);
	free(headerComprimido);
}
int existeArchivo(const char *ruta)
{
    FILE *archivo;
    if ((archivo = fopen(ruta, "r")))
    {
        fclose(archivo);
        return true;
    }
    return false;
}
void handshake(int socket, char operacion) {
	logearInfo("Enviando saludo al servidor");

	enviarHeader(socket,operacion,0);

	char* respuesta = malloc(32);
	int bytesRecibidos = recv(socket, respuesta, 32, 0);

	if (bytesRecibidos > 0) {
		logearInfo("Saludo recibido: \"%s\"", respuesta);
	}
	else {
		logearError("Ripeaste papu",true);
	}

	free(respuesta);
}
void logearError(char* formato, int terminar , ...) {
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
void logearInfo(char* formato, ...) {
	va_list args;
	va_start(args, formato);
	char* mensaje = malloc(512);
	vsnprintf(mensaje,512,formato,args);
	log_info(logger,mensaje);
	vprintf(formato,args);printf("\n");
	va_end(args);
	free(mensaje);
}
void serializarHeader(headerDeLosRipeados *header, void *buffer) {
	short *pBytesDePayload = (short*) buffer;
	*pBytesDePayload = header->bytesDePayload;
	char *pCodigoDeOperacion = (char*)(pBytesDePayload + 1);
	*pCodigoDeOperacion = header->codigoDeOperacion;
}
