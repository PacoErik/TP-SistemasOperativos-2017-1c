#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "commons/log.h"
#include "commons/config.h"
#include "commons/string.h"
#include <stdarg.h>

#define RUTA_CONFIG "config.cfg"
#define RUTA_LOG "consola.log"

t_log* logger;
t_config* config;

enum CodigoDeOperacion {
	MENSAJE, CONSOLA, MEMORIA, FILESYSTEM, CPU
};

// Para definirlo se puede usar tanto "headerDeLosRipeados" como "struct headerDeLosRipeados"
typedef struct headerDeLosRipeados {
	unsigned short bytesDePayload;
	char codigoDeOperacion; // 0 (mensaje). Handshake: 1 (consola), 2 (memoria), 3 (filesystem), 4 (cpu), 5 (kernel)
}__attribute__((packed, aligned(1))) headerDeLosRipeados;

int servidor; //kernel
char IP_KERNEL[16]; // 255.255.255.255 = 15 caracteres + 1 ('\0')
int PUERTO_KERNEL;

void imprimirOpcionesDeConsola();
void iniciarPrograma();
void desconectarPrograma();
void desconectarConsola();
void enviarMensaje();
void leerMensaje();
void limpiarPantalla();
void interaccionConsola();
void establecerConfiguracion();
void configurar(char*);
void conectarAKernel();
void handshake(int,char);
void serializarHeader(headerDeLosRipeados *, void *);
void deserializarHeader(headerDeLosRipeados *, void *);
void logearInfo(char *, ...);
void logearError(char *, int, ...);

int main(void) {

	configurar("consola");
	conectarAKernel();
	handshake(servidor, CONSOLA);
	interaccionConsola();

	return 0;
}

void imprimirOpcionesDeConsola() {
	printf("\n--------------------\n");
	printf("\n");
	printf("BIEVENIDO A LA CONSOLA\n");
	printf("SUS OPCIONES:\n");
	printf("\n");
	printf("1. Iniciar programa AnSISOP\n");
	printf("2. Finalizar programa AnSISOP\n");
	printf("3. Desconectar consola\n");
	printf("4. Enviar mensaje\n");
	printf("5. Limpiar mensajes\n");
	printf("6. Mostrar opciones nuevamente\n");
	printf("\n");
	printf("--------------------\n");
}

void iniciarPrograma() {
	printf("TODO\n");
} // TODO

void desconectarPrograma() {
	printf("TODO\n");
} // TODO

void desconectarConsola() {
	close(servidor);
	exit(EXIT_SUCCESS);
}

void enviarMensaje() {
	char mensaje[512] = "";

EscribirMensaje:
	printf("\nEscribir mensaje: ");
	scanf("%511[^\n]", mensaje);

	if (strlen(mensaje) == 0) {
		printf("Capo, hacé bien el mensaje"); // El mensaje no puede ser vacio
		// Limpiar buffer de entrada
		int c;
		while ( (c = getchar()) != '\n' && c != EOF );
		goto EscribirMensaje;
	}

	headerDeLosRipeados headerDeMiMensaje;
	headerDeMiMensaje.bytesDePayload = strlen(mensaje);
	headerDeMiMensaje.codigoDeOperacion = MENSAJE;

	int headerSize = sizeof(headerDeMiMensaje);
	void *headerComprimido = malloc(headerSize);
	serializarHeader(&headerDeMiMensaje, headerComprimido);

	send(servidor, headerComprimido, headerSize, 0); // Mando el header primero
	free(headerComprimido);

	send(servidor, mensaje, strlen(mensaje) + 1, 0); // Mando el mensaje después

	// El server retransmite el mensaje
	//leerMensaje();
	//Pero el kernel no se lo retransmite a la Consola (los otros procesos
	//esperan mensajes, este proceso espera que el usuario haga algo)
}

void leerMensaje() {

	int bytesRecibidos;
	char mensaje[512];
	bytesRecibidos = recv(servidor,&mensaje,sizeof(mensaje), 0);
	mensaje[bytesRecibidos]='\0';
	if(bytesRecibidos <= 0 || !strncmp(mensaje,"Error, desconectado",18)){
		close(servidor);
		logearError("Servidor desconectado luego de intentar leer mensaje",true);
	}
	logearInfo("Mensaje recibido: %s\n",mensaje);
}

void limpiarPantalla() {
	printf("\033[H\033[J");
}

void interaccionConsola() {
	imprimirOpcionesDeConsola();
	char input[3];
	while(1) {
		scanf("%2s", input);

		// limpiar buffer de entrada
		int c;
		while ( (c = getchar()) != '\n' && c != EOF );

		// Si lo que ingresa el usuario tiene mas de un caracter o no es numero
		if ( (strlen(input) != 1)
				|| '1' > input[0] || input[0] > '6' ) {
			printf("\nColoque una opcion correcta (1, 2, 3, 4, 5 o 6)\n");

			continue;
		}

		char opcion = input[0];

		switch(opcion) {
			case '1' : {
				iniciarPrograma(); // TODO
				logearInfo("Comando de inicio de programa ejecutado");
				break;
			}
			case '2' : {
				desconectarPrograma(); // TODO
				logearInfo("Comando de desconexión de programa ejecutado");
				break;
			}
			case '3' : {
				logearInfo("Comando de apagado de consola ejecutado\n");
				log_destroy(logger);
				desconectarConsola();
				break;
			}
			case '4' : {
				enviarMensaje();
				logearInfo("Comando de envío de mensaje ejecutado\n");
				leerMensaje();
				logearInfo("Mensaje completado, coloque otra opción. Opcion 6 para más información\n");
				break;
			}
			case '5' : {
				limpiarPantalla();
				break;
			}
			case '6' : {
				imprimirOpcionesDeConsola();
				break;
			}
		}
	}
}

void establecerConfiguracion() {
	if(config_has_property(config, "PUERTO_KERNEL")){
		PUERTO_KERNEL = config_get_int_value(config, "PUERTO_KERNEL");;
		logearInfo("Puerto Kernel: %d \n",PUERTO_KERNEL);
	}else{
		logearError("Error al leer el puerto del Kernel",true);
	}
	if(config_has_property(config, "IP_KERNEL")){
		strcpy(IP_KERNEL,config_get_string_value(config, "IP_KERNEL"));
		logearInfo("IP Kernel: %s \n",IP_KERNEL);
	}else{
		logearError("Error al leer la IP del Kernel",true);
	}
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

void conectarAKernel() {
	struct sockaddr_in direccionServidor;
	direccionServidor.sin_family = AF_INET;
	direccionServidor.sin_addr.s_addr = inet_addr( (char*) IP_KERNEL);
	direccionServidor.sin_port = htons(PUERTO_KERNEL);
	servidor = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(servidor, (struct sockaddr *) &direccionServidor,sizeof(direccionServidor)) < 0) {
		close(servidor);
		logearError("No se pudo conectar al Kernel",true);
	}
	logearInfo("Conectado al Kernel\n");
}

void configurar(char* quienSoy) {

	//Esto es por una cosa rara del Eclipse que ejecuta la aplicación
	//como si estuviese en la carpeta esther/consola/
	//En cambio, en la terminal se ejecuta desde esther/consola/Debug
	//pero en ese caso no existiria el archivo config ni el log
	//y es por eso que tenemos que leerlo desde el directorio anterior

	if (existeArchivo(RUTA_CONFIG)) {
		config = config_create(RUTA_CONFIG);
		logger = log_create(RUTA_LOG, quienSoy, false, LOG_LEVEL_INFO);
	}else{
		config = config_create(string_from_format("../%s",RUTA_CONFIG));
		logger = log_create(string_from_format("../%s",RUTA_LOG), quienSoy, false, LOG_LEVEL_INFO);
	}

	//Si la cantidad de valores establecidos en la configuración
	//es mayor a 0, entonces configurar la ip y el puerto,
	//sino, estaría mal hecho el config.cfg

	if(config_keys_amount(config) > 0) {
		establecerConfiguracion();
	} else {
		logearError("Error al leer archivo de configuración",true);
	}
	config_destroy(config);
}

void handshake(int socket, char operacion) {
	logearInfo("Conectando a servidor 0%%\n");
	headerDeLosRipeados handy;
	handy.bytesDePayload = 0;
	handy.codigoDeOperacion = operacion;

	int buffersize = sizeof(headerDeLosRipeados);
	void *buffer = malloc(buffersize);
	serializarHeader(&handy, buffer);
	send(socket, buffer, buffersize, 0);

	char respuesta[1024];
	int bytesRecibidos = recv(socket, (void *) &respuesta, sizeof(respuesta), 0);

	if (bytesRecibidos > 0) {
		logearInfo("Conectado a servidor 100%%\n");
		logearInfo("Mensaje del servidor: \"%s\"\n", respuesta);
	}
	else {
		logearError("Ripeaste\n",true);
	}
	free(buffer);
}

void serializarHeader(headerDeLosRipeados *header, void *buffer) {
	short *pBytesDePayload = (short*) buffer;
	*pBytesDePayload = header->bytesDePayload;
	char *pCodigoDeOperacion = (char*)(pBytesDePayload + 1);
	*pCodigoDeOperacion = header->codigoDeOperacion;
}

void deserializarHeader(headerDeLosRipeados *header, void *buffer) {
	short *pBytesDePayload = (short*) buffer;
	header->bytesDePayload = *pBytesDePayload;
	char *pCodigoDeOperacion = (char*)(pBytesDePayload + 1);
	header->codigoDeOperacion = *pCodigoDeOperacion;
}

void logearInfo(char* formato, ...) {
	char* mensaje;
	va_list args;
	va_start(args, formato);
	mensaje = string_from_vformat(formato,args);
	log_info(logger,mensaje);
	printf("%s", mensaje);
	va_end(args);
}

void logearError(char* formato, int terminar , ...) {
	char* mensaje;
	va_list args;
	va_start(args, terminar);
	mensaje = string_from_vformat(formato,args);
	log_error(logger,mensaje);
	printf("%s",mensaje);
	va_end(args);
	if (terminar==true) exit(0);
}
