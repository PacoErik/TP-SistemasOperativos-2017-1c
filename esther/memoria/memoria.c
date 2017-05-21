//..........HEADERS..........//
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
#include <pthread.h>
#include "commons/collections/list.h"

//..........DEFINES..........//
#define MOSTRAR_LOGS_EN_PANTALLA true
#define RUTA_CONFIG "config.cfg"
#define RUTA_LOG "memoria.log"
#define MAX_NUM_CLIENTES 100
#define ID_CLIENTE(x) ID_CLIENTES[x]
#define DEF_MISMO_SOCKET(SOCKET)										\
		_Bool mismoSocket(void* elemento) {								\
			return SOCKET == ((miCliente *) elemento)->socketCliente;	\
		}
enum CodigoDeOperacion {CONSOLA,MEMORIA,FILESYSTEM,CPU,KERNEL,MENSAJE,INICIAR_PROGRAMA,FINALIZAR_PROGRAMA,ERROR_MULTIPROGRAMACION};
enum estadoDelSectorDeMemoria {
	LIBRE, HEAP, USADO
};
#define DIVIDE_ROUNDUP(x,y) ((x - 1) / y + 1)

//..........ESTRUCTURAS..........//
typedef struct datosMemoria{
	int pid;
	char *code;
	short int codeSize;
}__attribute__((packed, aligned(1))) datosMemoria;

typedef struct headerDeLosRipeados {
	unsigned short bytesDePayload;
	char codigoDeOperacion;
}__attribute__((packed, aligned(1))) headerDeLosRipeados;

typedef struct miCliente {
	short socketCliente;
	char identificador;
} miCliente;

typedef struct informacion {
/*
 * DATA
 * De acá se calcula cuantos bytes ocupa la DATA,
 * y se sabe a que posicion de pagina colocar un Heap.
 */
} informacion;

typedef struct contenido {
	char PID;
	informacion data;
} contenido;

typedef struct heapMetaData {
	uint32_t tamanio;
	bool estaLibre;
} heapMetaData;

typedef struct pagina {
	char quienLaUsa; // LIBRE, HEAP, USADO
	heapMetaData heap; // quienLoUsa = HEAP
	contenido contenido; // quienLoUsa = USADO
} pagina;

typedef struct frame {
	bool enUso;
	pagina *posicion; // Hacer malloc(MARCO_SIZE)
} frame;

typedef struct frameDeCache {
	pagina *posicion; // Hacer malloc(MARCO_SIZE de Cache)
} cache;

typedef struct estructuraAdministrativa {
	int frame;
	int pid;
	int pag;
} estructuraAdministrativa;

typedef t_list listaCliente;

//.....VARIABLES GLOBALES.....//
t_log* logger;
t_config* config;
listaCliente *clientes;
char *memoria;
char PUERTO[6];
unsigned short MARCOS;
unsigned short MARCO_SIZE;
unsigned short ENTRADAS_CACHE;
unsigned short CACHE_X_PROC;
char REEMPLAZO_CACHE[8]; // ?
unsigned short RETARDO;
static const char *ID_CLIENTES[] = { "Consola", "Memoria", "File System", "CPU","Kernel" };
estructuraAdministrativa *tablaAdministrativa; //Marcos representa el total de frames, ver config.cfg TODO

//.......... PROTOTIPOS DE FUNCIONES..........//
void serializarHeader(headerDeLosRipeados *, void *);
void deserializarHeader(headerDeLosRipeados *, void *);
void crearMemoria(char **mem);
int hayAlguienQueSea(char identificacion);
void *fHilo(void *numCliente);
void *get_in_addr(struct sockaddr *);
void analizarCodigosDeOperacion(int, char, miCliente *);
void leerMensaje(int, int);
void establecerConfiguracion();
void configurar(char*);
void cerrarConexion(int, char*);
void agregarCliente(char, int);
void borrarCliente(int);
void logearInfo(char *, ...);
void logearError(char *, int, ...);
void interaccionMemoria();
void iniciarPrograma(int numCliente,unsigned short bytesDePayload);
void finalizarPrograma(int numCliente,unsigned short payload);
int existeArchivo(const char *ruta);
void configurarRetardo();
void dump();
void flush();
void imprimirOpcionesDeMemoria();
void size();
void limpiarPantalla();
void inicializarTabla();
void actualizar(datosMemoria);
int frameLibre();

//........PROCEDIMIENTO PRINCIPAL..........//
int main(void) {

	crearMemoria(&memoria); // Creacion de la memoria general.
	//cache miCache[ENTRADAS_CACHE]; // La cache de nuestra memoria
	configurar("memoria");

	clientes = list_create();

	tablaAdministrativa = malloc(sizeof(estructuraAdministrativa) * MARCOS);
	inicializarTabla();

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
		logearError("No se pudo abrir la memoria\n", true);
	}

	int servidor; // socket de escucha

	struct addrinfo *p; // Puntero para recorrer la lista de direcciones

	// Recorrer la lista hasta encontrar una direccion disponible para el bind
	for (p = direcciones; p != NULL; p = p->ai_next) {

		servidor = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (servidor == -1) {	// Devuelve 0 si hubo error
			continue;
		}

		// Para no tener que esperar al volver a usar el mismo puerto o socket
		int activado = 1;
		setsockopt(servidor, SOL_SOCKET, SO_REUSEADDR, &activado,
				sizeof(activado));

		if (bind(servidor, p->ai_addr, p->ai_addrlen) == 0) {
			break; // Se encontro una direccion disponible
		}

		close(servidor);
	}

	if (p == NULL) {
		logearError("Fallo al bindear el puerto\n", true);
	}

	freeaddrinfo(direcciones); // No necesito mas la lista de direcciones

	if (listen(servidor, 10) == -1) {
		logearError("Fallo al escuchar\n", true);
	}
	logearInfo("Estoy escuchando\n");

	//unsigned long int idHilos[10]={0};

//	interaccionMemoria();
	for(;;) {

		// Funcion Bloqueante

		// chequear idHilo

		//
		struct sockaddr_in direccionCliente;
		pthread_t tid; // Identificador del hilo
		pthread_attr_t atributos; // Atributos del hilo(pordefecto)
		pthread_attr_init(&atributos);
		socklen_t addrlen = sizeof direccionCliente;
		int nuevoCliente; // Socket del nuevo cliente conectado
		nuevoCliente = accept(servidor, (struct sockaddr *) &direccionCliente,
				&addrlen);

		int *param = malloc(sizeof(int));
		*param = nuevoCliente;

		pthread_create(&tid, &atributos, &fHilo, param);

		char direccionIP[INET_ADDRSTRLEN]; // string que contiene la direccion IP del cliente
		inet_ntop(AF_INET, get_in_addr((struct sockaddr*) &direccionCliente),
				direccionIP, INET_ADDRSTRLEN);
		logearInfo("Nueva conexión desde %s en el socket %d\n", direccionIP,
				nuevoCliente);
		//

	}
}

//............DEFINICION DE FUNCIONES................//

void crearMemoria(char **mem) {

	*mem = malloc(MARCOS * MARCO_SIZE);

}



void inicializarTabla() { //Aca inicializamos los pid en -2 (usado) y marcamos en la tabla los frames usados por la misma (pid -1)
	int i;
	int framesOcupadosPorTabla;
	int tamanioTotalTabla;

	for (i = 0; i < MARCOS; i++) {
		tablaAdministrativa[i].pid = -2;
	}

	tamanioTotalTabla = sizeof(estructuraAdministrativa) * MARCOS;
	framesOcupadosPorTabla = DIVIDE_ROUNDUP(framesOcupadosPorTabla, MARCO_SIZE);

	for (i = 0; i < framesOcupadosPorTabla; i++) {
		tablaAdministrativa[i].pid = -1;

	}

}

int recibirHandshake(int socket) {
	int buffersize = sizeof(headerDeLosRipeados);
	void *buffer = malloc(buffersize);
    int bytesRecibidos = recv(socket, buffer, buffersize, 0);
	if (bytesRecibidos <= 0) {
		if (bytesRecibidos == -1) {
			cerrarConexion(socket, "El socket %d se desconectó");
		}
		else {
			cerrarConexion(socket, "Socket %d: Error en el recv");
		}
		return -1;
	}
	headerDeLosRipeados handy;
	deserializarHeader(&handy, buffer);
	free(buffer);
	char codOp = handy.codigoDeOperacion;
	return (codOp == KERNEL || codOp == CPU) ? codOp : -1;
}

void leerMensaje(int socket, int bytes) {
	char* mensaje = calloc(bytes+1, sizeof(char));
	recv(socket, mensaje, bytes, 0);
	logearInfo("Mensaje recibido: %s\n",mensaje);
	free(mensaje);
}

void atenderKernel(int socketKernel) {
	int buffersize = sizeof(headerDeLosRipeados);
	void *buffer = malloc(buffersize);
	int bytes;
	headerDeLosRipeados header;
	while (bytes = recv(socketKernel, buffer, buffersize, 0)) {
		deserializarHeader(&header, buffer);
		switch(header.codigoDeOperacion) {
			case MENSAJE:
				leerMensaje(socketKernel, header.bytesDePayload);
				break;
			case INICIAR_PROGRAMA:
				iniciarPrograma(socketKernel,header.bytesDePayload);
				break;
			case FINALIZAR_PROGRAMA:
				finalizarPrograma(socketKernel,header.bytesDePayload);
				break;
			default:
				printf("TODO\n");
				break;
		}
	}
	if (bytes == -1) {
		cerrarConexion(socketKernel, "Socket %d: Error en el recv");
	}
	else {
		cerrarConexion(socketKernel, "El socket %d se desconectó");
	}
	free(buffer);
}

/*
void atenderKernel(int socketCliente, void* bufferHeader) {

	headerDeLosRipeados header;
	deserializarHeader(&header, bufferHeader);

	switch (header.codigoDeOperacion) {
	case INICIAR_PROGRAMA:
		iniciarPrograma(socketCliente,header.bytesDePayload);
		break;
	case FINALIZAR_PROGRAMA:
		finalizarPrograma(socketCliente,header.bytesDePayload);
		break;
	}

}
*/

void *fHilo(void* param) {
	int socketCliente = (int)*((int*)param);
	int tipoCliente = recibirHandshake(socketCliente);
	if (tipoCliente == -1) {
		// La memoria no conoce otro tipo de clientes ni permite hacer operaciones sin haber hecho handshake
		cerrarConexion(socketCliente, "Socket %d: Operacion Invalida");
		return NULL;
	}
	if (tipoCliente == KERNEL) {
		printf("Kernel\n");
		if (hayAlguienQueSea(KERNEL)) {
			cerrarConexion(socketCliente, "El cliente %i intentó conectarse como Kernel ya habiendo uno");
			return NULL;
		}
		send(socketCliente, "Bienvenido", sizeof "Bienvenido", 0);
		agregarCliente(KERNEL, socketCliente);
		atenderKernel(socketCliente);
	}
	else {
		agregarCliente(CPU, socketCliente);
		//atenderCPU(socketCliente);
	}
	return NULL;
}

/*
void *fHilo(void *runner_param) {
	int buffersize = sizeof(headerDeLosRipeados);
	char* buffer = malloc(buffersize);
	int socket = *(int*)runner_param;

	int bytesRecibidos = recv(socket, buffer, buffersize, 0);

	if (bytesRecibidos <= 0) {
		if (bytesRecibidos == 0) {
			cerrarConexion(socket, "El socket %d se desconectó\n");
		} else {
			logearError("Error en el recv\n", false);
			close(socket);
		}
		borrarCliente(socket);

	} else {
		// llegó info, vamos a ver el header
		int estado = analizarHeader(socket, buffer);

		char *respuesta;

		switch (estado) {

		case CPU:
			respuesta = "Header del CPU recibido";
			send(socket, respuesta, strlen(respuesta) + 1, 0);
			while (1) {
				int bytesRecibidos = recv(socket, buffer, buffersize, 0);

				if (bytesRecibidos <= 0) {
					if (bytesRecibidos == 0) {
						cerrarConexion(socket, "El socket %d se desconectó\n");
					} else {
						logearError("Error en el recv\n", false);
						close(socket);
					}
				} else {
					analizarHeaderMemoria(socket, buffer);
				}
			}
			break;

		case KERNEL:
			respuesta = "Header del Kernel recibido";
			send(socket, respuesta, strlen(respuesta) + 1, 0);

			while (1) {

				int bytesRecibidos = recv(socket, buffer, buffersize, 0); //Este recv toma el header para saber que operacion ejecutar

				if (bytesRecibidos <= 0) {
					if (bytesRecibidos == 0) {
						cerrarConexion(socket, "El socket %d se desconectó\n");
					} else {
							logearError("Error en el recv\n", false);
							close(socket);
						}
				} else {
					analizarHeaderMemoria(socket, buffer); //Este analizador recibe el codigo de op y ejecuta su funcion correspondiente.
				}

			}
			break;
		case -1:
			send(socket, "Error, desconectado", 20, 0);
			borrarCliente(socket);
			cerrarConexion(socket, "El socket %d hizo una operación inválida\n"); // Estaría bueno que por cada valor negativo hacer un código de error para decirle al usuario en qué la cagó.
			break;
		}
	}
}
*/

int frameLibre(){
	int i=0;

	while(tablaAdministrativa[i].pid!=-2){
		i++;
	}
	return i;
}


void actualizar(datosMemoria datosMem){

	memcpy(memoria+(frameLibre()*MARCO_SIZE),datosMem.code,datosMem.codeSize);

	tablaAdministrativa[frameLibre()].frame = frameLibre();
	tablaAdministrativa[frameLibre()].pag = 0;
	tablaAdministrativa[frameLibre()].pid = datosMem.pid;

	free(datosMem.code);

}

void iniciarPrograma(int numCliente,unsigned short bytesDePayload){

	// payload "identificador del programa + codigo + tamanio del codigo".
	int buffersize = bytesDePayload;
	char* buffer = malloc(buffersize);
	int bytesRecibidos = recv(numCliente, buffer, buffersize, 0); //Recibo la informacion del programa a iniciar

	//"desserializo" lo que recibi en el buffer
	datosMemoria datosMem;
	memcpy(&datosMem.pid, buffer, sizeof(int));
	memcpy(&datosMem.codeSize, buffer + sizeof(int), sizeof(datosMem.codeSize));
	datosMem.code = malloc(datosMem.codeSize);
	memcpy(datosMem.code, buffer + sizeof(int) + sizeof(short int),datosMem.codeSize);

	printf("Codigo del Kernel:\n %s \n",datosMem.code);

	actualizar(datosMem);

	printf("Llegue\n");
}

void finalizarPrograma(int numCliente,unsigned short payload){
	// payload "identificador del programa".
	int buffersize = sizeof(payload);
	char* buffer = malloc(buffersize);
	int bytesRecibidos = recv(numCliente, buffer, buffersize, 0);
	// eliminar entradas en la estructura.

}

int existeArchivo(const char *ruta) {
	FILE *archivo;
	if ((archivo = fopen(ruta, "r"))) {
		fclose(archivo);
		return true;
	}
	return false;
}

int hayAlguienQueSea(char identificacion) {
	bool mismoID(void* elemento) {
		return identificacion == ((miCliente*) elemento)->identificador;
	}
	return list_any_satisfy(clientes, mismoID);
}

void *get_in_addr(struct sockaddr *sa) {
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*) sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*) sa)->sin6_addr);
}

void agregarCliente(char identificador, int socketCliente) {
	if (existeCliente(socketCliente)) {
		logearError("No se puede agregar 2 veces mismo socket", false);
		return;
	}

	miCliente *cliente = malloc(sizeof (miCliente));
	cliente->identificador = identificador;
	cliente->socketCliente = socketCliente;

	list_add(clientes, cliente);
}

void borrarCliente(int socketCliente) {
	DEF_MISMO_SOCKET(socketCliente);
	list_remove_and_destroy_by_condition(clientes, mismoSocket, free);
}

int existeCliente(int socketCliente) {
	DEF_MISMO_SOCKET(socketCliente);
	return list_any_satisfy(clientes, mismoSocket);
}

int tipoCliente(int socketCliente) {
	DEF_MISMO_SOCKET(socketCliente);
	miCliente *found = (miCliente*)(list_find(clientes, mismoSocket));
	if (found == NULL) {
		return -1;
	}
	return found->identificador;
}

void cerrarConexion(int socketCliente, char* motivo) {
	logearInfo(motivo, socketCliente);
	borrarCliente(socketCliente); // Si el cliente no esta en la lista no hace nada
	close(socketCliente);
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
		config = config_create(string_from_format("../%s", RUTA_CONFIG));
		logger = log_create(string_from_format("../%s", RUTA_LOG), quienSoy,
		false, LOG_LEVEL_INFO);
	}

	//Si la cantidad de valores establecidos en la configuración
	//es mayor a 0, entonces configurar la ip y el puerto,
	//sino, estaría mal hecho el config.cfg

	if (config_keys_amount(config) > 0) {
		establecerConfiguracion();
	} else {
		logearError("Error al leer archivo de configuración", true);
	}
	config_destroy(config);
}

void configurarRetardo() {
	printf("El actual retardo es %i ms\n", RETARDO);
	printf("Coloque nuevo retardo (0ms - 9999ms):\n");

	int i;

	char input[5];

	for (i = 0; i < 5; i++) { // Lo limpiamos
		input[i] = '\0';
	}

	i = 0;

	scanf("%s", input);

	while (i != 5) { // Chequeamos si todos son digitos
		if (isdigit(input[i])) {
			i++;
		} else {
			if (input[i] == '\0' && i != 0) { // Si estamos posicionados en un fin de string y no es el primer valor (string nulo)
				short exRETARDO = RETARDO;
				RETARDO = atoi(input);
				printf("Retardo cambiado a %i ms\n", RETARDO);
				logearInfo(
						"Comando de configuracion de retardo ejecutado. Fue cambiado de %i ms a %i ms\n",
						exRETARDO, RETARDO);
				break;
			} else {
				printf(
						"Coloque digitos validos. Ingrese 6 para obtener nuevamente las opciones\n");
				break;
			}
		}
	}
}

void serializarHeader(headerDeLosRipeados *header, void *buffer) {
	short *pBytesDePayload = (short*) buffer;
	*pBytesDePayload = header->bytesDePayload;
	char *pCodigoDeOperacion = (char*) (pBytesDePayload + 1);
	*pCodigoDeOperacion = header->codigoDeOperacion;
}

void deserializarHeader(headerDeLosRipeados *header, void *buffer) {
	short *pBytesDePayload = (short*) buffer;
	header->bytesDePayload = *pBytesDePayload;
	char *pCodigoDeOperacion = (char*) (pBytesDePayload + 1);
	header->codigoDeOperacion = *pCodigoDeOperacion;
}

void logearInfo(char* formato, ...) {
	char* mensaje;
	va_list args;
	va_start(args, formato);
	mensaje = string_from_vformat(formato, args);
	log_info(logger, mensaje);
	printf("%s", mensaje);
	va_end(args);
}

void logearError(char* formato, int terminar, ...) {
	char* mensaje;
	va_list args;
	va_start(args, terminar);
	mensaje = string_from_vformat(formato, args);
	log_error(logger, mensaje);
	printf("%s", mensaje);
	va_end(args);
	if (terminar == true) {
		exit(EXIT_FAILURE);
	}
}

void establecerConfiguracion() {
	if (config_has_property(config, "PUERTO")) {
		strcpy(PUERTO, config_get_string_value(config, "PUERTO"));
		logearInfo("PUERTO: %s \n", PUERTO);
	} else {
		logearError("Error al leer el puerto de la memoria", true);
	}

	if (config_has_property(config, "MARCOS")) {
		MARCOS = config_get_int_value(config, "MARCOS");
		logearInfo("MARCOS: %i \n", MARCOS);
	} else {
		logearError("Error al leer los marcos de la memoria", true);
	}

	if (config_has_property(config, "MARCO_SIZE")) {
		MARCO_SIZE = config_get_int_value(config, "MARCO_SIZE");
		logearInfo("MARCO_SIZE: %i \n", MARCO_SIZE);
	} else {
		logearError("Error al leer los tamaños de los marcos de la memoria",
		true);
	}

	if (config_has_property(config, "ENTRADAS_CACHE")) {
		ENTRADAS_CACHE = config_get_int_value(config, "ENTRADAS_CACHE");
		logearInfo("ENTRADAS_CACHE: %i \n", ENTRADAS_CACHE);
	} else {
		logearError("Error al leer las entradas cache de la memoria", true);
	}

	if (config_has_property(config, "CACHE_X_PROC")) {
		ENTRADAS_CACHE = config_get_int_value(config, "CACHE_X_PROC");
		logearInfo("CACHE_X_PROC: %i \n", CACHE_X_PROC);
	} else {
		logearError("Error al leer los cache por proceso de la memoria", true);
	}

	if (config_has_property(config, "REEMPLAZO_CACHE")) {
		strcpy(REEMPLAZO_CACHE,
				config_get_string_value(config, "REEMPLAZO_CACHE"));
		logearInfo("REEMPLAZO_CACHE: %s \n", REEMPLAZO_CACHE);
	} else {
		logearError("Error al leer los reemplazo cache de la memoria", true);
	}

	if (config_has_property(config, "RETARDO")) {
		RETARDO = config_get_int_value(config, "RETARDO");
		logearInfo("RETARDO: %i \n", RETARDO);
	} else {
		logearError("Error al leer el retardo de la memoria", true);
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

void imprimirOpcionesDeMemoria() {
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

	while (1) {
		scanf("%2s", input);

		// limpiar buffer de entrada

		int c;
		while ((c = getchar()) != '\n' && c != EOF)
			;

		// Si lo que ingresa el usuario tiene mas de un caracter o no es numero

		if ((strlen(input) != 1) || '1' > input[0] || input[0] > '6') {
			printf("\nColoque una opcion correcta (1, 2, 3, 4, 5 o 6)\n");
			continue;
		}

		char opcion = input[0];
		switch (opcion) {
		case '1': {
			configurarRetardo();
			break;
		}
		case '2': {
			logearInfo("Comando de dump ejecutado");
			dump(); // TODO
			break;
		}
		case '3': {
			logearInfo("Comando de flush ejecutado\n");
			flush(); // TODO
			break;
		}
		case '4': {
			size(); // TODO
			break;
		}
		case '5': {
			limpiarPantalla();
			break;
		}
		case '6': {
			imprimirOpcionesDeMemoria();
			break;
		}
		}
	}
}
