/*----------HEADERS----------*/
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
#include "qepd/qepd.h"

#include "op_kernel.h"

/*----------DEFINES----------*/
#define MOSTRAR_LOGS_EN_PANTALLA true
#define RUTA_CONFIG "config.cfg"
#define RUTA_LOG "memoria.log"
#define MAX_NUM_CLIENTES 100
//#define ID_CLIENTE(x) ID_CLIENTES[x]
#define DEF_MISMO_SOCKET(SOCKET)										\
		_Bool mismoSocket(void* elemento) {								\
			return SOCKET == ((miCliente *) elemento)->socketCliente;	\
		}
enum estadoDelSectorDeMemoria {
	LIBRE, HEAP, USADO
};
#define DIVIDE_ROUNDUP(x,y) ((x - 1) / y + 1)

#define FRAME_ADMIN -1
#define FRAME_LIBRE -2

/*----------ESTRUCTURAS----------*/
typedef struct datosMemoria {
	int pid;
	short int codeSize;
	char *code;
}__attribute__((packed, aligned(1))) datosMemoria;

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
}__attribute__((packed, aligned(1))) estructuraAdministrativa;

typedef t_list listaCliente;

/*-----VARIABLES GLOBALES-----*/

/* Configs */
int					PUERTO;
int					MARCOS;
int					MARCO_SIZE;
int					ENTRADAS_CACHE;
int					CACHE_X_PROC;
char				REEMPLAZO_CACHE[8]; // ?
unsigned short		RETARDO;

t_log* logger;
t_config* config;

listaCliente *clientes;

char *memoria;
estructuraAdministrativa *tablaAdministrativa; //Marcos representa el total de frames, ver config.cfg TODO

/*-----------PROTOTIPOS DE FUNCIONES----------*/

int			asignar_frames_contiguos			(int, int, size_t, void*);
void		atenderKernel						(int);
void		agregarCliente						(char, int);
void		borrarCliente						(int);
void		cerrarConexion						(int, char*);
void		configurarRetardo					();
void		crearMemoria						(void);
void		dump								();
void		establecerConfiguracion			();
int			existeCliente						(int);
void*		fHilo								(void *);
void		finalizarPrograma					(int, unsigned short);
void		flush								();
int			hayAlguienQueSea					(char);
void		imprimirOpcionesDeMemoria			();
void		inicializarTabla					();
void*		interaccionMemoria				(void *);
char*		ir_a_frame							(int);
void		limpiarPantalla					();
int			recibirHandshake					(int);
void		size								();
int			tipoCliente						(int);

/*--------PROCEDIMIENTO PRINCIPAL----------*/
int main(void) {
	configurar("memoria");

	crearMemoria(); // Creacion de la memoria general.
	//cache miCache[ENTRADAS_CACHE]; // La cache de nuestra memoria

	clientes = list_create();

	inicializarTabla();

	pthread_t hilo_consola;
	pthread_create(&hilo_consola, NULL, interaccionMemoria, NULL);

	int servidor = socket(AF_INET, SOCK_STREAM, 0);	// Socket de escucha

	if (servidor == -1) {
		logearError("No se pudo crear el socket", true);
	}

	int activado = 1;
	setsockopt(servidor, SOL_SOCKET, SO_REUSEADDR, &activado, sizeof(activado));

	struct sockaddr_in servidor_info;

	servidor_info.sin_family = AF_INET;
	servidor_info.sin_port = htons(PUERTO);
	servidor_info.sin_addr.s_addr = INADDR_ANY;
	bzero(&(servidor_info.sin_zero), 8);

	if (bind(servidor, (struct sockaddr*) &servidor_info, sizeof(struct sockaddr)) == -1) {
		logearError("Fallo al bindear el puerto", true);
	}

    if (listen(servidor, 10) == -1) {
		logearError("Fallo al escuchar", true);
    }

	logearInfo("Estoy escuchando");

	for(;;) {
		int nuevoCliente;					// Socket del nuevo cliente conectado

		struct sockaddr_in clienteInfo;
		socklen_t addrlen = sizeof clienteInfo;

		nuevoCliente = accept(servidor, (struct sockaddr *) &clienteInfo, &addrlen);

		if (nuevoCliente == -1) {
			logearError("Fallo en el accept", false);
		}

		int *param = malloc(sizeof(int));
		*param = nuevoCliente;

		pthread_t tid;						// Identificador del hilo
		pthread_attr_t atributos;			// Atributos del hilo

		pthread_attr_init(&atributos);
		pthread_attr_setdetachstate(&atributos, PTHREAD_CREATE_DETACHED);	// Detachable

		pthread_create(&tid, &atributos, &fHilo, param);

		logearInfo("Nueva conexión desde %s en el socket %d",
				inet_ntoa(clienteInfo.sin_addr), nuevoCliente);
	}
}

/*------------DEFINICION DE FUNCIONES----------------*/

/*
 * Asigna frames contiguos para un proceso.
 */
int asignar_frames_contiguos(int PID, int frames, size_t bytes, void *datos) {
	int i;
	int frames_encontrados;		// Contador frames libres encontrados

	for (i = 0, frames_encontrados = 0;
			i < MARCOS && frames_encontrados < frames; i++) {
		if (tablaAdministrativa[i].pid == FRAME_LIBRE) {
			frames_encontrados++;
		}
		else {
			frames_encontrados = 0;
		}
	}

	/* No hay frames disponibles */
	if (frames_encontrados != frames) {
		return 0;
	}

	/* Asignar frames al proceso */

	int frame_inicial = i - frames;		// Indice primer frame
	int frame_final = i;				// Indice ultimo frame

	for (i = frame_inicial; i < frame_final; i++) {
		tablaAdministrativa[i].pid = PID;
		tablaAdministrativa[i].pag = i - frame_inicial;
	}

	/* Escribir datos a la memoria */
	memcpy(ir_a_frame(frame_inicial), datos, sizeof(char) * bytes);

	return 1;
}

/*
 * Devuelve puntero al frame en la posicion indicada.
 */
char *ir_a_frame(int indice) {
	if (indice >= MARCOS) {
		return NULL;
	}
	return ((char (*) [MARCO_SIZE]) memoria) [indice];
}

void liberar_frames(int PID) {
	int i;
	for (i = 0; i < MARCOS; i++) {
		if (tablaAdministrativa[i].pid == PID) {
			tablaAdministrativa[i].pid = FRAME_LIBRE;
		}
	}
}

void atenderKernel(int socketKernel) {
	int ret;
	while (1) {
		ret = kernel_processar_operacion(socketKernel);

		if (ret == -1) {
			cerrarConexion(socketKernel, "Error de conexion con el Kernel (socket %d)");
			break;
		}

		if (ret == 0) {
			cerrarConexion(socketKernel, "El Kernel hizo una operacion invalida (socket %d)");
			break;
		}
	}
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

void cerrarConexion(int socketCliente, char* motivo) {
	logearInfo(motivo, socketCliente);
	borrarCliente(socketCliente); // Si el cliente no esta en la lista no hace nada
	close(socketCliente);
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

void crearMemoria(void) {
	memoria = malloc(MARCOS * MARCO_SIZE);
}

void dump() {
	char tmp[16];

	int pag_digits;			// Cantidad de digitos hexa de una pagina
	int offset_digits;		// Cantidad de digitos hexa de un desplazamiento

	snprintf(tmp, sizeof tmp - 1, "%x", MARCOS);
	pag_digits = strlen(tmp);

	snprintf(tmp, sizeof tmp - 1, "%x", MARCO_SIZE);
	offset_digits = strlen(tmp);

	int i_pag, i_offset;	// Indice pagina y desplazamiento
	for (i_pag = 0; i_pag < MARCOS; i_pag++) {
		for (i_offset = 0; i_offset < MARCO_SIZE; i_offset += 16) {
			printf("%0*X:%0*X ", pag_digits, i_pag, offset_digits, i_offset);

			//printf("%.*s\n", 16, (char *) ir_a_frame(i_pag) + i_offset);
			fwrite((char *) ir_a_frame(i_pag) + i_offset, sizeof(char), 16, stdout);
			printf("\n");

			fflush(stdout);
		}
	}
}

void establecerConfiguracion() {
	if (config_has_property(config, "PUERTO")) {
		PUERTO = config_get_int_value(config, "PUERTO");
		logearInfo("PUERTO: %i", PUERTO);
	}
	else {
		logearError("Error al leer el puerto de la memoria", true);
	}

	if (config_has_property(config, "MARCOS")) {
		MARCOS = config_get_int_value(config, "MARCOS");
		logearInfo("MARCOS: %i", MARCOS);
	}
	else {
		logearError("Error al leer los marcos de la memoria", true);
	}

	if (config_has_property(config, "MARCO_SIZE")) {
		MARCO_SIZE = config_get_int_value(config, "MARCO_SIZE");
		logearInfo("MARCO_SIZE: %i", MARCO_SIZE);
	}
	else {
		logearError("Error al leer los tamaños de los marcos de la memoria",
		true);
	}

	if (config_has_property(config, "ENTRADAS_CACHE")) {
		ENTRADAS_CACHE = config_get_int_value(config, "ENTRADAS_CACHE");
		logearInfo("ENTRADAS_CACHE: %i", ENTRADAS_CACHE);
	}
	else {
		logearError("Error al leer las entradas cache de la memoria", true);
	}

	if (config_has_property(config, "CACHE_X_PROC")) {
		ENTRADAS_CACHE = config_get_int_value(config, "CACHE_X_PROC");
		logearInfo("CACHE_X_PROC: %i", CACHE_X_PROC);
	}
	else {
		logearError("Error al leer los cache por proceso de la memoria", true);
	}

	if (config_has_property(config, "REEMPLAZO_CACHE")) {
		strcpy(REEMPLAZO_CACHE,
				config_get_string_value(config, "REEMPLAZO_CACHE"));
		logearInfo("REEMPLAZO_CACHE: %s", REEMPLAZO_CACHE);
	}
	else {
		logearError("Error al leer los reemplazo cache de la memoria", true);
	}

	if (config_has_property(config, "RETARDO")) {
		RETARDO = config_get_int_value(config, "RETARDO");
		logearInfo("RETARDO: %i", RETARDO);
	}
	else {
		logearError("Error al leer el retardo de la memoria", true);
	}
}

int existeCliente(int socketCliente) {
	DEF_MISMO_SOCKET(socketCliente);
	return list_any_satisfy(clientes, mismoSocket);
}

void *fHilo(void* param) {
	int socketCliente = (int)*((int*)param);
	int tipoCliente = recibirHandshake(socketCliente);
	if (tipoCliente == -1) {
		// La memoria no conoce otro tipo de clientes ni permite hacer operaciones sin haber hecho handshake
		cerrarConexion(socketCliente, "Socket %d: Operacion Invalida");
		return NULL;
	}
	if (tipoCliente == KERNEL) {
		if (hayAlguienQueSea(KERNEL)) {
			cerrarConexion(socketCliente, "El cliente %i intentó conectarse como Kernel ya habiendo uno");
			return NULL;
		}

		printf("Kernel conectado\n");

		send(socketCliente, "Bienvenido", sizeof "Bienvenido", 0);

		/* Enviar tamanio de pagina (marco) al kernel */
		send(socketCliente, &MARCO_SIZE, sizeof(int), 0);

		agregarCliente(KERNEL, socketCliente);
		atenderKernel(socketCliente);
	}
	else {
		agregarCliente(CPU, socketCliente);
		//atenderCPU(socketCliente);
	}
	return NULL;
}

void finalizarPrograma(int numCliente, unsigned short payload) {
	// payload "identificador del programa".
	int PID;
	recv(numCliente, &PID, sizeof(PID), 0);
	// eliminar entradas en la estructura.

}

void flush() {
	// TODO
}

int hayAlguienQueSea(char identificacion) {
	bool mismoID(void* elemento) {
		return identificacion == ((miCliente*) elemento)->identificador;
	}
	return list_any_satisfy(clientes, mismoID);
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

/*
 * Aca inicializamos los pid en -2 (usado) y marcamos en la tabla los frames usados por la misma (pid -1)
 */
void inicializarTabla(void) {
	// Aca no necesita ningun malloc, se guarda directamente en memoria
	tablaAdministrativa = (estructuraAdministrativa *) memoria;

	int tamanioTotalTabla = sizeof(estructuraAdministrativa[MARCOS]);
	int framesOcupadosPorTabla = DIVIDE_ROUNDUP(tamanioTotalTabla, MARCO_SIZE);

	int i;
	for (i = 0 ; i < framesOcupadosPorTabla; i++) {
		tablaAdministrativa[i].pid = FRAME_ADMIN;
	}

	for ( ; i < MARCOS; i++) {
		tablaAdministrativa[i].pid = FRAME_LIBRE;
	}
}

void *interaccionMemoria(void * _) {
	imprimirOpcionesDeMemoria();
	char input[3];

	while (1) {
		scanf("%2s", input);

		// limpiar buffer de entrada

		int c;
		while ((c = getchar()) != '\n' && c != EOF);

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
			dump();
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

int recibirHandshake(int socket) {
	headerDeLosRipeados header;
	int bytesRecibidos = recibirHeader(socket, &header);
	if (bytesRecibidos <= 0) {
		if (bytesRecibidos == -1) {
			cerrarConexion(socket, "El socket %d se desconectó");
		}
		else {
			cerrarConexion(socket, "Socket %d: Error en el recv");
		}
		return -1;
	}
	char codOp = header.codigoDeOperacion;
	return (codOp == KERNEL || codOp == CPU) ? codOp : -1;
}

void limpiarPantalla() {
	printf("\033[H\033[J");
}

void size() {
	// TODO
}

int tipoCliente(int socketCliente) {
	DEF_MISMO_SOCKET(socketCliente);
	miCliente *found = (miCliente*)(list_find(clientes, mismoSocket));
	if (found == NULL) {
		return -1;
	}
	return found->identificador;
}
