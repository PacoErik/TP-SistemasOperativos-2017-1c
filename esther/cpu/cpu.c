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
#include <pthread.h>

#define RUTA_CONFIG "config.cfg"
#define RUTA_LOG "cpu.log"
#define MAX_THREADS 2

t_log* logger;
t_config* config;

enum CodigoDeOperacion {
	MENSAJE, // Mensaje a leer
	CONSOLA, MEMORIA, FILESYSTEM, CPU, KERNEL, // Identificador de un proceso
	EXCEPCION_DE_SOLICITUD, // Mensajes provenientes de procesos varios
	ABRIR_ARCHIVO, LEER_ARCHIVO, ESCRIBIR_ARCHIVO, CERRAR_ARCHIVO, FINALIZAR_PROGRAMA // Mensajes provenientes de Kernel
};

typedef struct headerDeLosRipeados {
	unsigned short bytesDePayload;
	char codigoDeOperacion; // 0 (mensaje). Handshake: 1 (consola), 2 (memoria), 3 (filesystem), 4 (cpu), 5 (kernel). 6 en adelante son códigos de distintos procesos.
}__attribute__((packed, aligned(1))) headerDeLosRipeados;

char IP_KERNEL[16]; // 255.255.255.255 = 15 caracteres + 1 ('\0')
int PUERTO_KERNEL;
char IP_MEMORIA[16];
int PUERTO_MEMORIA;

typedef struct instruccionUtil {
	char offsetInicio;
	char offsetFin;
} instruccionUtil;

typedef struct indiceCodigo {
	instruccionUtil *instrucciones;
} indiceCodigo;

typedef struct indiceEtiquetas {
	// Diccionario what the actual fuck
} indiceEtiquetas;

typedef struct indiceStack {
	// Lista de argumentos
	// Lista de variables
	// Direccion de retorno
	// Posicion de la variable de retorno
} indiceStack;

typedef struct PCB {
	char processID;
	char programCounter;
	char paginasDeCodigo;
	indiceCodigo indiceDeCodigo;
	indiceEtiquetas indiceDeEtiquetas;
	indiceStack indiceDeStack;
	int8_t exitCode;
} PCB;

typedef struct servidor { // Esto es más que nada una cheteada para poder usar al socket/identificador como constante en los switch
	int socket;
	char identificador;
} servidor;

servidor kernel;
servidor memoria;

void establecerConfiguracion();
void configurar(char*);
void handshake(int, char);
void serializarHeader(headerDeLosRipeados *, void *);
void deserializarHeader(headerDeLosRipeados *, void *);
void logearInfo(char *, ...);
void logearError(char *, int, ...);
void conectarA(char* IP, int PUERTO, char identificador);
void leerMensaje(servidor servidor, short bytesDePayload);
void analizarCodigosDeOperacion(servidor servidor, char codigoDeOperacion);
void cumplirDeseosDeKernel(char codigoDeOperacion);
void cumplirDeseosDeMemoria(char codigoDeOperacion);
void finalizarPrograma();
void* atenderKernel();
void* atenderMemoria();

int main(void) {

	configurar("cpu");

	conectarA(IP_KERNEL, PUERTO_KERNEL, KERNEL);
	conectarA(IP_MEMORIA, PUERTO_MEMORIA, MEMORIA);

	handshake(kernel.socket, CPU);
	handshake(memoria.socket, CPU);

	pthread_t hilo[MAX_THREADS];

//	pthread_create(&hilo[0], NULL, &atenderKernel, NULL);
//	pthread_create(&hilo[1], NULL, &atenderMemoria, NULL);

	return 0;
}

void* atenderKernel(void) {
	while(1) {

	}

	return NULL;
}

void* atenderMemoria(void) {
	while(1) {

	}

	return NULL;
}

/*
 * ↓ Llega información de alguno de los servidores ↓
 */

/**
 * Analiza el contenido del header, y respecto a ello realiza distintas acciones
 * devuelve -1 si el servidor causa problemas
 */
int analizarHeader(servidor servidor, void* bufferHeader) {
    headerDeLosRipeados header;
    deserializarHeader(&header, bufferHeader);

    if (header.codigoDeOperacion == MENSAJE) {
        if (header.bytesDePayload <= 0) {
            logearError("El cliente %i intentó mandar un mensaje sin contenido\n", false, servidor.socket);
            return -1;
        }
        else {
            leerMensaje(servidor, header.bytesDePayload);
        }
    } else {
    	analizarCodigosDeOperacion(servidor, header.codigoDeOperacion);
    }

    return 0;
}


void analizarCodigosDeOperacion(servidor servidor, char codigoDeOperacion) {

	switch(servidor.identificador) {
		case KERNEL:
			cumplirDeseosDeKernel(codigoDeOperacion);
			break;

		case MEMORIA:
			cumplirDeseosDeMemoria(codigoDeOperacion);
			break;

		default:
			// TODO
			exit(EXIT_FAILURE);
	}
}


void cumplirDeseosDeKernel(char codigoDeOperacion) {
	switch(codigoDeOperacion) {
		case EXCEPCION_DE_SOLICITUD:
			logearError("Excepcion al solicitar al Kernel", false);
			finalizarPrograma();
			break;

		case ABRIR_ARCHIVO:
			// TODO
			break;

		case LEER_ARCHIVO:
			// TODO
			break;

		case ESCRIBIR_ARCHIVO:
			// TODO
			break;

		case CERRAR_ARCHIVO:
			// TODO
			break;

		case FINALIZAR_PROGRAMA:
			finalizarPrograma();
			break;

		default:
			printf("TODO");
			// TODO
	}
}

void cumplirDeseosDeMemoria(char codigoDeOperacion) {
	switch(codigoDeOperacion) {
		case EXCEPCION_DE_SOLICITUD:
			logearError("Excepcion al solicitar a la memoria", false);
			finalizarPrograma();
			break;

		default:
			printf("TODO");
			// TODO
	}
}

/*
 * ↑ Llega información de alguno de los servidores ↑
 */

/*
 * ↓ Acatar ordenes de algunos de los servidores ↓
 */

void finalizarPrograma() {

}

void leerMensaje(servidor servidor, short bytesDePayload) {
    char* mensaje = malloc(bytesDePayload+1);
    recv(servidor.socket, mensaje, bytesDePayload,0);
    mensaje[bytesDePayload]='\0';
    logearInfo("Mensaje recibido: %s\n", mensaje);
    free(mensaje);
}

/*
 * ↑ Acatar ordenes de algunos de los servidores ↑
 */

/*
 * ↓ Configuración del CPU y conexión a los servidores ↓
 */

void establecerConfiguracion() {
	if(config_has_property(config, "PUERTO_KERNEL")) {
		PUERTO_KERNEL = config_get_int_value(config, "PUERTO_KERNEL");
		logearInfo("Puerto Kernel: %i \n",PUERTO_KERNEL);
	} else {
		logearError("Error al leer el puerto del Kernel", true);
	}
	if(config_has_property(config, "IP_KERNEL")) {
		strcpy(IP_KERNEL,config_get_string_value(config, "IP_KERNEL"));
		logearInfo("IP Kernel: %s \n", IP_KERNEL);
	} else {
		logearError("Error al leer la IP del Kernel", true);
	}
	if(config_has_property(config, "PUERTO_MEMORIA")) {
		PUERTO_MEMORIA = config_get_int_value(config, "PUERTO_MEMORIA");
		logearInfo("Puerto Memoria: %i \n", PUERTO_MEMORIA);
	} else {
		logearError("Error al leer el puerto de la Memoria", true);
	}
	if(config_has_property(config, "IP_MEMORIA")){
		strcpy(IP_MEMORIA,config_get_string_value(config, "IP_MEMORIA"));
		logearInfo("IP Memoria: %s \n", IP_MEMORIA);
	} else {
		logearError("Error al leer la IP de la Memoria", true);
	}

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
	} else {
		config = config_create(string_from_format("../%s",RUTA_CONFIG));
		logger = log_create(string_from_format("../%s",RUTA_LOG), quienSoy,false, LOG_LEVEL_INFO);
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

void conectarA(char* IP, int PUERTO, char identificador) {

	struct sockaddr_in direccionServidor;
	direccionServidor.sin_family = AF_INET;
	direccionServidor.sin_addr.s_addr = inet_addr((char*) IP);
	direccionServidor.sin_port = htons(PUERTO);

	int servidor = socket(AF_INET, SOCK_STREAM, 0);

	char* quienEs;
	quienEs = malloc(8);

	if(identificador == KERNEL) {
		strcpy(quienEs, "Kernel");
		kernel.socket = servidor;
		kernel.identificador = identificador;
	} else {
		strcpy(quienEs, "Memoria");
		memoria.socket = servidor;
		memoria.identificador = identificador;
	}

	if (connect(servidor, (struct sockaddr *) &direccionServidor, sizeof(direccionServidor)) < 0) {
		close(servidor);
		logearError("No se pudo conectar a %s", true, quienEs);
	}

	logearInfo("Conectado a %s\n", quienEs);
	free(quienEs);
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
		logearInfo("Conectado a servidor 100 porciento\n");
		logearInfo("Mensaje del servidor: \"%s\"\n", respuesta);
	}
	else {
		logearError("Ripeaste\n",true);
	}
	free(buffer);
}

/*
 * ↑ Configuración del CPU y conexión a los servidores ↑
 */

/*
 * ↓ Logeos de CPU ↓
 */

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
	printf("%s", mensaje);
	va_end(args);
	if (terminar==true) exit(0);
}

/*
 * ↑ Logeos del CPU ↑
 */

/*
 * ↓ Funciones auxiliares del CPU ↓
 */

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

/*
 * ↑ Funciones auxiliares del CPU ↑
 */
