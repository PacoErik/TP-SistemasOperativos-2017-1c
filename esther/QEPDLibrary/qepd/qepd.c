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

		char *ruta = strdup(quienSoy);
		char *timestamp = obtener_timestamp();

		string_append(&ruta, "_");
		string_append(&ruta, timestamp);
		string_append(&ruta, ".log");

		free(timestamp);

		logger = log_create(ruta, quienSoy, false, LOG_LEVEL_INFO);
		free(ruta);
	}
	else {
		logear_error("No existe el archivo de configuración", true);
	}

	if (config_keys_amount(config) > 0) {
		establecer_configuracion();
	}
	else {
		logear_error("Error al leer archivo de configuración", true);
	}

	config_destroy(config);
}
void enviar_header(int socket, char operacion, int bytes) {
	headerDeLosRipeados headerDeMiMensaje;
	headerDeMiMensaje.bytesDePayload = bytes;
	headerDeMiMensaje.codigoDeOperacion = operacion;
	void* header = &headerDeMiMensaje;
	int headerSize = sizeof(headerDeMiMensaje);
	send(socket, header, headerSize, MSG_NOSIGNAL);
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

	char* respuesta = malloc(sizeof("Bienvenido!"));
	int bytesRecibidos = recv(socket, respuesta, sizeof("Bienvenido!"), 0);

	if (bytesRecibidos > 0) {
		logear_info("Saludo recibido: \"%s\"", respuesta);
	}
	else {
		logear_error("Ripeaste papu",true);
	}

	free(respuesta);
}
int recibir_header(int socket, headerDeLosRipeados *header) {
	int bytesARecibir = sizeof(headerDeLosRipeados);
	int bytesRecibidos = recv(socket, header, bytesARecibir, 0);

	return bytesRecibidos;
}

char *obtener_timestamp(void) {
	struct tm date_time;
	struct timeb time_n;

	char *time_str = strdup("YYYYmmddHHMMSSms");

	ftime(&time_n);
	localtime_r(&time_n.time, &date_time);

	strftime(time_str, 49, "%Y%m%d%H%M%S", &date_time);
	snprintf(&time_str[14], 3, "%hu", time_n.millitm);

	return time_str;
}
