/*
 * memoria.c
 *
 *  Created on: 13/4/2017
 *      Author: utnso
 */
/*
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
#define RUTA_LOG "memoria.log"

t_log* logger;
t_config* config;

enum CodigoDeOperacion {
	MENSAJE, CONSOLA, MEMORIA, FILESYSTEM, CPU
};

typedef struct headerDeLosRipeados {
	unsigned short bytesDePayload;
	char codigoDeOperacion; // 0 (mensaje). Handshake: 1 (consola), 2 (memoria), 3 (filesystem), 4 (cpu), 5 (kernel)
}__attribute__((packed, aligned(1))) headerDeLosRipeados;



void desconectarConsola();
void leerMensaje();
void establecerConfiguracion();
void configurar(char*);
void conectarAKernel();
void handshake(int, char);
void serializarHeader(headerDeLosRipeados *, void *);
void deserializarHeader(headerDeLosRipeados *, void *);
void logearInfo(char *, ...);
void logearError(char *, int, ...);

int main(void) {

	configurar("memoria");
	while (1){
		leerMensaje();
	}

	return 0;
}


void desconectarConsola() {
	close(servidor);
	exit(EXIT_SUCCESS);
}

void leerMensaje() {

	int bytesRecibidos;
	char mensaje[512];
	bytesRecibidos = recv(servidor, &mensaje, sizeof(mensaje), 0);
	mensaje[bytesRecibidos]='\0';
	if(bytesRecibidos <= 0){
		close(servidor);
		logearError("Servidor desconectado luego de intentar leer mensaje",true);
	}
	logearInfo("Mensaje recibido: %s\n",mensaje);
}

void establecerConfiguracion() {
	if(config_has_property(config, "PUERTO_KERNEL")) {
		PUERTO_KERNEL = config_get_int_value(config, "PUERTO_KERNEL");
		logearInfo("Puerto Kernel: %d \n",PUERTO_KERNEL);
	} else {
		logearError("Error al leer el puerto del Kernel",true);
	}
	if(config_has_property(config, "IP_KERNEL")) {
		strcpy(IP_KERNEL,config_get_string_value(config, "IP_KERNEL"));
		logearInfo("IP Kernel: %s \n",IP_KERNEL);
	} else {
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
	printf("%s", mensaje);
	va_end(args);
	if (terminar==true) exit(0);
}
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "commons/log.h"
#include "commons/config.h"
#include "commons/string.h"
#include <stdarg.h>
#include <ctype.h>

#define MOSTRAR_LOGS_EN_PANTALLA true

#define RUTA_CONFIG "config.cfg"
#define RUTA_LOG "memoria.log"

t_log* logger;
t_config* config;

char PUERTO[6];
short MARCOS;
short MARCO_SIZE;
short ENTRADAS_CACHE;
short CACHE_X_PROC;
char REEMPLAZO_CACHE[8]; // ?
short RETARDO_MEMORIA;

#define MAX_NUM_CLIENTES 100

enum CodigoDeOperacion {
    MENSAJE, CONSOLA, MEMORIA, FILESYSTEM, CPU, KERNEL
};

#define ID_CLIENTE(x) ID_CLIENTES[x-1]

static const char *ID_CLIENTES[] = {
	"Consola", "Memoria", "File System", "CPU", "Kernel"
};

typedef struct headerDeLosRipeados {
    unsigned short bytesDePayload;
    char codigoDeOperacion; // 0 (mensaje). Handshake: 1 (consola), 2 (memoria), 3 (filesystem), 4 (cpu), 5 (kernel)
}__attribute__((packed, aligned(1))) headerDeLosRipeados;

typedef struct miCliente {
    short socketCliente;
    char identificador;
} miCliente;

void limpiarClientes(miCliente *);
void analizarCodigosDeOperacion(int , char , miCliente *);
int analizarHeader(int , void* , miCliente *);
void leerMensaje(int , short , miCliente *);
void establecerConfiguracion();
void configurar(char*);
void cerrarConexion(int , char* );
int posicionSocket(int , miCliente *);
void agregarCliente(char , int , miCliente *);
void borrarCliente(int , miCliente *);
void serializarHeader(headerDeLosRipeados *, void *);
void deserializarHeader(headerDeLosRipeados *, void *);
void logearInfo(char *, ...);
void logearError(char *, int, ...);
int hayAlguienQueSea(char identificacion, miCliente *clientes);

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(void) {
	configurar("memoria");

	miCliente misClientes[MAX_NUM_CLIENTES];
    limpiarClientes(misClientes);

    int buffersize = sizeof(headerDeLosRipeados);
    char* buffer = malloc(buffersize);

    struct addrinfo hints; // Le da una idea al getaddrinfo() el tipo de info que debe retornar
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = AI_PASSIVE;


    /* getaddrinfo() retorna una lista de posibles direcciones para el bind */

	struct addrinfo *direcciones; // lista de posibles direcciones para el bind
    int rv = getaddrinfo(NULL, PUERTO, &hints, &direcciones); // si devuelve 0 hay un error
    if (rv != 0) {
    	// gai_strerror() devuelve el mensaje de error segun el codigo de error
        logearError("No se pudo abrir la memoria\n",true);
    }

    int servidor; // socket de escucha

    struct addrinfo *p; // Puntero para recorrer la lista de direcciones

    // Recorrer la lista hasta encontrar una direccion disponible para el bind
    for (p = direcciones; p != NULL; p = p->ai_next) {

    	servidor = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (servidor == -1) {	// Devuelve 0 si hubo error
            continue;
        }

        int activado = 1;
        setsockopt(servidor, SOL_SOCKET, SO_REUSEADDR, &activado, sizeof(activado));

        if (bind(servidor, p->ai_addr, p->ai_addrlen) == 0) {
            break; // Se encontro una direccion disponible
        }

        close(servidor);
    }

    if (p == NULL) {
        logearError("Fallo al bindear el puerto\n",true);
    }

    freeaddrinfo(direcciones); // No necesito mas la lista de direcciones

    if (listen(servidor, 10) == -1) {
        logearError("Fallo al escuchar\n",true);
    }
    logearInfo("Estoy escuchando\n");

    fd_set conectados; // Set de FDs conectados
    fd_set read_fds; // sockets de lectura

    FD_ZERO(&conectados);
    FD_ZERO(&read_fds);

    FD_SET(servidor, &conectados);

    int fdmax;	// valor maximo de los FDs
    fdmax = servidor; // Por ahora hay un solo socket, por eso es el maximo

//	interaccionMemoria();
    for(;;) {
        read_fds = conectados;
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            logearError("Error en el select\n",true);
        }

        // Se detecta algun cambio en alguno de los sockets

        struct sockaddr_in direccionCliente;

        int i;
        for(i = 0; i <= fdmax; i++) {
        	if (FD_ISSET(i, &read_fds) == 0) { // No hubo cambios en este socket
        		continue;
        	}
        	// Se detecta un cambio en el socket de escucha => hay un nuevo cliente
			if (i == servidor) {
		        socklen_t addrlen = sizeof direccionCliente;
		        int nuevoCliente; // Socket del nuevo cliente conectado
				nuevoCliente = accept(servidor, (struct sockaddr *)&direccionCliente, &addrlen);

				if (nuevoCliente == -1) {
					logearError("Fallo en el accept\n",false);
				}
				else {
					FD_SET(nuevoCliente, &conectados); // Agregarlo al set
					if (nuevoCliente > fdmax) {
						fdmax = nuevoCliente; // Cambia el maximo
					}
				    char direccionIP[INET_ADDRSTRLEN]; // string que contiene la direccion IP del cliente
					inet_ntop(AF_INET,	get_in_addr((struct sockaddr*) &direccionCliente), direccionIP, INET_ADDRSTRLEN);
					logearInfo("Nueva conexión desde %s en el socket %d\n", direccionIP, nuevoCliente);
				}
			}
			// Un cliente mando un mensaje
			else {
			    int bytesRecibidos = recv(i, buffer, buffersize + 1, 0);
				if (bytesRecibidos <= 0) {
					if (bytesRecibidos == 0) {
						cerrarConexion(i, "El socket %d se desconectó\n");
					}
					else {
						logearError("Error en el recv\n",false);
						close(i);
					}
					borrarCliente(i, misClientes);
					FD_CLR(i, &conectados);
				}
				else {
					// llegó info, vamos a ver el header
					int estado = analizarHeader(i, buffer, misClientes);

					if (estado < 0) { //ocurrió algún problema con ese socket (puede ser un cliente o no)
						send(i, "Error, desconectado", 20, 0);
						borrarCliente(i, misClientes);
						cerrarConexion(i, "El socket %d hizo una operación inválida\n"); // Estaría bueno que por cada valor negativo hacer un código de error para decirle al usuario en qué la cagó.
						FD_CLR(i, &conectados);
						continue; //salteamos esta iteración
					}
					char *respuesta = "Header recibido";
					send(i, respuesta, strlen(respuesta) + 1, 0);
				}
			}
        }
    }
    free(buffer);
    return 0;
}

void agregarCliente(char identificador, int socketCliente, miCliente *clientes) {
	int i;
	for (i=0; i<MAX_NUM_CLIENTES; i++) {
		if (clientes[i].socketCliente == -1) { //woohoo, encontramos un espacio libre
			clientes[i].socketCliente = socketCliente;
			clientes[i].identificador = identificador;
			logearInfo("El nuevo cliente fue identificado como: %s\n", ID_CLIENTE(identificador));
			break;
		}
	}

}

void borrarCliente(int socketCliente, miCliente *clientes) {
	int posicion = posicionSocket(socketCliente,clientes);
	if (posicion >= 0) {
		clientes[posicion].socketCliente = -1;
		clientes[posicion].identificador = 255;
	}
}

/**
 * Dado el array de clientes, te dice en que posición hay un espacio libre.
 * Si no la hay, retorna -1.
 */
int hayLugar(miCliente *clientes) {
	int i;
	for (i=0; i<MAX_NUM_CLIENTES; i++) {
		if (clientes[i].socketCliente == -1) { //woohoo, encontramos un espacio libre
			return i;
		}
	}

	return -1;
}

/**
 * Analiza el contenido del header, y respecto a ello realiza distintas acciones
 * devuelve -1 si el socket causa problemas
 */
int analizarHeader(int socketCliente, void* bufferHeader, miCliente *clientes) {
    headerDeLosRipeados header;
    deserializarHeader(&header, bufferHeader);
    int posicionDelSocket = posicionSocket(socketCliente,clientes);

    if (header.codigoDeOperacion == CPU || header.codigoDeOperacion == KERNEL) {
    	if(hayLugar(clientes) != -1) { // Si hay lugar para alguien más
			// No estaba antes en el array de clientes
			if (posicionDelSocket < 0) {
				if(header.codigoDeOperacion != KERNEL) { // Si es una CPU que se conecte tranqui
						agregarCliente(header.codigoDeOperacion,socketCliente,clientes);
				} else { // Verificar si ya se metió un Kernel
					if(hayAlguienQueSea(KERNEL, clientes)) {
						logearError("El cliente %i intentó conectarse como Kernel ya habiendo uno\n",false,socketCliente); // Hay que logear acá?
						return -1;
					} else {
						agregarCliente(header.codigoDeOperacion,socketCliente,clientes);
					}
				}
			}
			else { //No se puede enviar un handshake 2 veces (el cliente ya estaba en el array de clientes)
				return -1; // Otro cacho de código se va a encargar de borrarlo
			}
    	} else {
    		return -1; // No hay lugar
    	}
    }

    else if (header.codigoDeOperacion == MENSAJE) {
        if (header.bytesDePayload <= 0) {
            logearError("El cliente %i intentó mandar un mensaje sin contenido\n",false,socketCliente);
            return -1;
        }
        else {
            leerMensaje(socketCliente, header.bytesDePayload, clientes);
        }
    }

    else {
    	if (posicionDelSocket >= 0) { //Si se encontró el cliente en la estructura de clientes (osea ya hizo handshake)
    		analizarCodigosDeOperacion(socketCliente, header.codigoDeOperacion, clientes); // TODO
    	}
    	else { //Header no reconocido, chau cliente intruso
    		return -1;
    	}
    }
    return 0;
}
/*
int enviarMensajeATodos(int socketCliente, char* mensaje, miCliente *clientes) {
	int cantidad = 0;
	int i;
	for (i=0;i<MAX_NUM_CLIENTES;i++) { //enviamos mensaje a los clientes registrados
		if (clientes[i].identificador >= MEMORIA && clientes[i].identificador <= CPU) { //solo le mandamos a MEMORIA,FILESYSTEM y CPU
			send(clientes[i].socketCliente,mensaje,strlen(mensaje),0);
			cantidad++;
		}
	}
	return cantidad;
}
*/

void leerMensaje(int socketCliente, short bytesDePayload, miCliente *clientes) {
    char* mensaje = malloc(bytesDePayload+1);
    recv(socketCliente,mensaje,bytesDePayload,0);
    mensaje[bytesDePayload]='\0';
    logearInfo("Mensaje recibido: %s\n",mensaje);
/*
    int cantidad = enviarMensajeATodos(socketCliente,mensaje, clientes);
    logearInfo("Mensaje retransmitido a %i clientes\n",cantidad);
*/
    free(mensaje);
}

/*
 * limpiarCliente(); esta copado porque te permite usarlo en distintas circunstancias
 */
void limpiarCliente(miCliente cliente) {
	cliente.socketCliente = -1;
	cliente.identificador = 255;
}

void limpiarClientes(miCliente *clientes) {
    int i;
    for(i = 0; i < MAX_NUM_CLIENTES; i++) {
    	limpiarCliente(*clientes);
/*
		clientes[i].socketCliente = -1;
        clientes[i].identificador = 255;
*/
    }
}

/**
 * Dado un Socket, te dice en que posición se encuentra en el array de Sockets
 */
int posicionSocket(int socketCliente, miCliente *clientes) {
    int i;
    for(i = 0; i < MAX_NUM_CLIENTES; i++) {
        if(clientes[i].socketCliente == socketCliente) {
            return i;
        }
    }
    return -1;
}

void analizarCodigosDeOperacion(int socketCliente, char codigoDeOperacion, miCliente *clientes) {
	int posicionDelSocket = posicionSocket(socketCliente, clientes);
    char codigoDelCliente = clientes[posicionDelSocket].identificador;
    switch(codigoDelCliente) {
        case KERNEL:
            // TODO
            break;
        case CPU:
            // TODO
            break;
        default:
            printf("TODO, cod. de operación recibido: %d\n",codigoDeOperacion);
            // TODO
    }
}

void cerrarConexion(int socketCliente, char* motivo){
	logearInfo(motivo, socketCliente);
	close(socketCliente);
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
	printf("%s", mensaje);
	va_end(args);
	if (terminar==true) {
		exit(EXIT_FAILURE);
	}
}

void establecerConfiguracion() {
	if(config_has_property(config, "PUERTO")) {
		strcpy(PUERTO,config_get_string_value(config, "PUERTO"));
		logearInfo("PUERTO: %s \n",PUERTO);
	} else {
		logearError("Error al leer el puerto de la memoria",true);
	}

	if(config_has_property(config, "MARCOS")) {
		MARCOS = config_get_int_value(config, "MARCOS");
		logearInfo("MARCOS: %s \n",MARCOS);
	} else {
		logearError("Error al leer los marcos de la memoria",true);
	}

	if(config_has_property(config, "MARCO_SIZE")) {
		MARCO_SIZE = config_get_int_value(config, "MARCO_SIZE");
		logearInfo("MARCO_SIZE: %s \n",MARCO_SIZE);
	} else {
		logearError("Error al leer los tamaños de los marcos de la memoria",true);
	}

	if(config_has_property(config, "ENTRADAS_CACHE")) {
		ENTRADAS_CACHE = config_get_int_value(config, "ENTRADAS_CACHE");
		logearInfo("ENTRADAS_CACHE: %s \n",ENTRADAS_CACHE);
	} else {
		logearError("Error al leer las entradas cache de la memoria",true);
	}

	if(config_has_property(config, "CACHE_X_PROC")) {
		ENTRADAS_CACHE = config_get_int_value(config, "CACHE_X_PROC");
		logearInfo("CACHE_X_PROC: %s \n",ENTRADAS_CACHE);
	} else {
		logearError("Error al leer los cache por proceso de la memoria",true);
	}

	if(config_has_property(config, "REEMPLAZO_CACHE")) {
		strcpy(REEMPLAZO_CACHE,config_get_string_value(config, "REEMPLAZO_CACHE"));
		logearInfo("REEMPLAZO_CACHE: %s \n",ENTRADAS_CACHE);
	} else {
		logearError("Error al leer los reemplazo cache de la memoria",true);
	}

	if(config_has_property(config, "RETARDO_MEMORIA")) {
		RETARDO_MEMORIA = config_get_int_value(config, "RETARDO_MEMORIA");
		logearInfo("RETARDO_MEMORIA: %s \n",ENTRADAS_CACHE);
	} else {
		logearError("Error al leer el retardo de la memoria",true);
	}
}

int existeArchivo(const char *ruta) {
    FILE *archivo;
    if ((archivo = fopen(ruta, "r")))
    {
        fclose(archivo);
        return true;
    }
    return false;
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

int hayAlguienQueSea(char identificacion, miCliente *clientes) {
	int i;
	for(i = 0; i < MAX_NUM_CLIENTES; i++) {
		if(clientes[i].identificador == identificacion){
			return i; // Por las dudas le das la posición
		}
	}

	return 0; // No lo encontró
}

void configurarRetardo() {
	printf("El actual retardo es %i ms\n", RETARDO_MEMORIA);
	printf("Coloque nuevo retardo (0ms - 9999ms):\n");

	int i;

	char input[5];

	for(i = 0; i < 5; i++) { // Lo limpiamos
		input[i] = '\0';
	}

	i = 0;

	scanf("%s", input);

	while(i != 5) { // Chequeamos si todos son digitos
		if(isdigit(input[i])) {
			i++;
		} else {
			if(input[i] == '\0' && i != 0) { // Si estamos posicionados en un fin de string y no es el primer valor (string nulo)
				short exRETARDO = RETARDO_MEMORIA;
				RETARDO_MEMORIA = atoi(input);
				printf("Retardo cambiado a %i ms\n", RETARDO_MEMORIA);
				logearInfo("Comando de configuracion de retardo ejecutado. Fue cambiado de %i ms a %i ms\n", exRETARDO, RETARDO_MEMORIA);
				break;
			} else {
				printf("Coloque digitos validos. Ingrese 6 para obtener nuevamente las opciones\n");
				break;
			}
		}
	}
}

void dump() {
	// TODO
}

void flush() {
	// TODO
}

void size() {
	// TODO
}

void limpiarPantalla() {
	printf("\033[H\033[J");
}

void imprimirOpcionesDeMemoria(){
	printf("\n--------------------\n");
	printf("\n");
	printf("BIEVENIDO A LA MEMORIA\n");
	printf("SUS OPCIONES:\n");
	printf("\n");
	printf("1. Configurar retardo\n");
	printf("2. Dump\n");
	printf("3. Flush\n");
	printf("4. Size\n");
	printf("5. Limpiar mensajes\n");
	printf("6. Mostrar opciones nuevamente\n");
	printf("\n");
	printf("--------------------\n");
}

void interaccionMemoria() {
	imprimirOpcionesDeMemoria();
	char input[3];

	/*
	 * Vamos a tener que usar hilos,
	 * ya que:
	 * 1) Tendriamos que interactuar con la memoria
	 * 2) Tendriamos que manejar los sockets y gilada
	 *
	 * Nachito
	 */

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
				configurarRetardo();
				break;
			}
			case '2' : {
				logearInfo("Comando de dump ejecutado");
				dump(); // TODO
				break;
			}
			case '3' : {
				logearInfo("Comando de flush ejecutado\n");
				flush(); // TODO
				break;
			}
			case '4' : {
				size(); // TODO
				break;
			}
			case '5' : {
				limpiarPantalla();
				break;
			}
			case '6' : {
				imprimirOpcionesDeMemoria();
				break;
			}
		}
	}
}
