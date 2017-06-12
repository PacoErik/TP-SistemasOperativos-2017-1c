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
#include "op_cpu.h"
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
} PACKED datosMemoria;

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

typedef t_list listaCliente;

/*-----VARIABLES GLOBALES-----*/

/* Configs */
int					PUERTO;
short int			RETARDO;
int					MARCOS;
int					MARCO_SIZE;
int					ENTRADAS_CACHE;
int					CACHE_X_PROC;
char				REEMPLAZO_CACHE[8]; // ?

t_log* logger;
t_config* config;

listaCliente *clientes;

char *memoria;
char *memoria_cache;

estructuraAdministrativa_cache *tablaAdministrativa_cache;
estructuraAdministrativa *tablaAdministrativa; //Marcos representa el total de frames, ver config.cfg TODO

/*-----------PROTOTIPOS DE FUNCIONES----------*/

int			asignar_frames_contiguos			(int, int, int, size_t, void*);
int			asignar_paginas_a_proceso			(int, int);
int			ultimaPaginaDeProceso 				(int);
void		atenderKernel						(int);
void 		atenderCPU							(int);
void		agregar_cliente						(char, int);
void		borrar_cliente						(int);
void		cerrar_conexion						(int, char*);
void		configurarRetardo					();
void		crearMemoria						(void);
void		dump								();
void		establecer_configuracion			();
int			existe_cliente						(int);
void*		fHilo								(void *);
void		finalizarPrograma					(int, unsigned short);
void		flush								();
int			hayAlguienQueSea					(char);
void		imprimirOpcionesDeMemoria			();
void		inicializarTabla					();
void		inicializarTabla_cache				();
void*		interaccionMemoria				(void *);
char*		ir_a_frame							(int);
void		limpiar_pantalla					();
int			recibir_handshake					(int);
void		size								();
int			tipo_cliente						(int);
int 		maxLru								();
int 		posicionAReemplazarDeCache			();
char*		remover_salto_linea					(char*);


/*--------PROCEDIMIENTO PRINCIPAL----------*/

int main(void) {
	configurar("memoria");

	crearMemoria(); // Creacion de la memoria general y cache

	clientes = list_create();

	inicializarTabla();
	inicializarTabla_cache();

	pthread_t hilo_consola;
	pthread_create(&hilo_consola, NULL, interaccionMemoria, NULL);

	int servidor = socket(AF_INET, SOCK_STREAM, 0);	// Socket de escucha

	if (servidor == -1) {
		logear_error("No se pudo crear el socket", true);
	}

	int activado = 1;
	setsockopt(servidor, SOL_SOCKET, SO_REUSEADDR, &activado, sizeof(activado));

	struct sockaddr_in servidor_info;

	servidor_info.sin_family = AF_INET;
	servidor_info.sin_port = htons(PUERTO);
	servidor_info.sin_addr.s_addr = INADDR_ANY;
	bzero(&(servidor_info.sin_zero), 8);

	if (bind(servidor, (struct sockaddr*) &servidor_info, sizeof(struct sockaddr)) == -1) {
		logear_error("Fallo al bindear el puerto", true);
	}

    if (listen(servidor, 10) == -1) {
		logear_error("Fallo al escuchar", true);
    }

	logear_info("Estoy escuchando");

	for(;;) {
		int nuevoCliente;					// Socket del nuevo cliente conectado

		struct sockaddr_in clienteInfo;
		socklen_t addrlen = sizeof clienteInfo;

		nuevoCliente = accept(servidor, (struct sockaddr *) &clienteInfo, &addrlen);

		if (nuevoCliente == -1) {
			logear_error("Fallo en el accept", false);
		}

		int *param = malloc(sizeof(int));
		*param = nuevoCliente;

		pthread_t tid;						// Identificador del hilo
		pthread_attr_t atributos;			// Atributos del hilo

		pthread_attr_init(&atributos);
		pthread_attr_setdetachstate(&atributos, PTHREAD_CREATE_DETACHED);	// Detachable

		pthread_create(&tid, &atributos, &fHilo, param);

		pthread_attr_destroy(&atributos);

		logear_info("Nueva conexión desde %s en el socket %d",
				inet_ntoa(clienteInfo.sin_addr), nuevoCliente);
	}
}

/*------------DEFINICION DE FUNCIONES----------------*/

// FUNCION DEL HILO

void *fHilo(void* param) {
	int socketCliente = (int)*((int*)param);
	int tipoCliente = recibir_handshake(socketCliente);
	if (tipoCliente == -1) {
		// La memoria no conoce otro tipo de clientes ni permite hacer operaciones sin haber hecho handshake
		cerrar_conexion(socketCliente, "Socket %d: Operacion Invalida");
		return NULL;
	}
	if (tipoCliente == KERNEL) {
		if (hayAlguienQueSea(KERNEL)) {
			cerrar_conexion(socketCliente, "El cliente %i intentó conectarse como Kernel ya habiendo uno");
			return NULL;
		}

		printf("Kernel conectado\n");

		send(socketCliente, "Bienvenido!", sizeof "Bienvenido!", 0);

		/* Enviar tamanio de pagina (marco) al kernel */
		send(socketCliente, &MARCO_SIZE, sizeof(int), 0);

		agregar_cliente(KERNEL, socketCliente);
		atenderKernel(socketCliente);
	}
	else {
		agregar_cliente(CPU, socketCliente);

		printf("Nuevo CPU conectado\n");
		send(socketCliente, "Bienvenido!", sizeof "Bienvenido!", 0);
		send(socketCliente, &MARCO_SIZE, sizeof(int), 0);
		atenderCPU(socketCliente);
	}
	return NULL;
}

// MANEJO DE MEMORIA

void crearMemoria(void) {
	memoria = calloc(MARCOS , MARCO_SIZE);

	memoria_cache = calloc(ENTRADAS_CACHE , MARCO_SIZE);

}

int maxLru(void){
	int i, max = 0;

	for (i = 0; i < ENTRADAS_CACHE; i++) {
		if (tablaAdministrativa_cache[i].lru > max)
			max = tablaAdministrativa_cache[i].lru;
	}

	return max;
}

int posicionAReemplazarDeCache(void){
	int i, min = 1000, pos = 0;

	for (i = 0; i < ENTRADAS_CACHE; i++) {
		if (tablaAdministrativa_cache[i].pid == FRAME_LIBRE)
			return i;
	}

	for (i = 0; i < ENTRADAS_CACHE; i++) {
		if (tablaAdministrativa_cache[i].lru < min) {
			min = tablaAdministrativa_cache[i].lru;
			pos = i;
		}
	}

	return pos;
}

/*
 * Aca inicializamos los pid en FRAME_LIBRE y marcamos en la tabla los frames usados por la misma FRAME_ADMIN
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

int buscarEnMemoriaYDevolver(posicionDeMemoriaVariable posicion, int socket) {

	int posMainMemory;

	//Recorro la tabla admin de la cache para ver si esta
	for (posMainMemory = 0; posMainMemory < ENTRADAS_CACHE; posMainMemory++) {
		if (tablaAdministrativa_cache[posMainMemory].pid == posicion.processID
				&& tablaAdministrativa_cache[posMainMemory].pag
						== posicion.numeroDePagina) {

			enviarDesdeCache(posMainMemory, socket, posicion);

			tablaAdministrativa_cache[posMainMemory].lru = maxLru() + 1; // en la posicion i de la tabla de cache,correspondiente al pid encontrado, se le asigna al campo lru el maximo lru encontrado + 1

			return 1;
		}
	}

	usleep(RETARDO * 1000);

	/* Recorro la tabla admin de la memoria principal */
	for (posMainMemory = 0; posMainMemory < MARCOS; posMainMemory++) {
		if (tablaAdministrativa[posMainMemory].pid == posicion.processID
				&& tablaAdministrativa[posMainMemory].pag
						== posicion.numeroDePagina) {

			//enviarDesdeMemoria();

			int posCache;
			posCache = posicionAReemplazarDeCache();

			tablaAdministrativa_cache[posCache].pid = posicion.processID;
			tablaAdministrativa_cache[posCache].pag = posicion.numeroDePagina;
			tablaAdministrativa_cache[posCache].lru = maxLru() + 1;

			// Se copia a memoria cache

			memcpy(
					memoria_cache
							+ tablaAdministrativa_cache[posCache].contenidoDeLaPag,
					memoria + MARCO_SIZE * posMainMemory, MARCO_SIZE);

			return 1;

		}
	}

	return 0;

}

void inicializarTabla_cache(void) {

	int i;

	tablaAdministrativa_cache = malloc(ENTRADAS_CACHE * sizeof(estructuraAdministrativa_cache));

	for (i=0 ; i < ENTRADAS_CACHE; i++) {
			tablaAdministrativa_cache[i].pid = FRAME_LIBRE;
			//La primer pag en cache empieza en 0 * MARCO_SIZE = 0, las sig posiciones de inicio se mueven de a MARCO_SIZE, por eso el i*MARCO_SIZE
			tablaAdministrativa_cache[i].contenidoDeLaPag = i * MARCO_SIZE;
		}

}

/*
 * Asigna frames contiguos para un proceso.
 */
int asignar_frames_contiguos(int PID, int frames_codigo, int frames_stack, size_t bytes, void *datos) {
	int i;
	int frames_encontrados;		// Contador frames libres encontrados
	int frames_totales = frames_stack + frames_codigo;

	for (i = 0, frames_encontrados = 0;
			i < MARCOS && frames_encontrados < frames_totales; i++) {
		if (tablaAdministrativa[i].pid == FRAME_LIBRE) {
			frames_encontrados++;
		}
		else {
			frames_encontrados = 0;
		}
	}

	/* No hay frames disponibles */
	if (frames_encontrados != frames_totales) {
		return 0;
	}

	/* Asignar frames al proceso */

	int frame_inicial = i - frames_totales;		// Indice primer frame
	int frame_final = i;				// Indice ultimo frame

	for (i = frame_inicial; i < frame_final; i++) {
		tablaAdministrativa[i].pid = PID;
		tablaAdministrativa[i].pag = i - frame_inicial;
	}

	/* Escribir datos a la memoria */
	memcpy(ir_a_frame(frame_inicial), datos, bytes);

	return 1;
}

int asignar_paginas_a_proceso(int pid, int framesNecesarios) {

		int i;
		int frames_encontrados;	// Contador frames libres encontrados
		int pagInicioHeap = ultimaPaginaDeProceso(pid);


		for (i = 0, frames_encontrados = 0;
				i < MARCOS && frames_encontrados < framesNecesarios; i++) {
			if (tablaAdministrativa[i].pid == FRAME_LIBRE) {
				frames_encontrados++;
			}
			else {
				frames_encontrados = 0;
			}
		}

		/* No hay frames disponibles */
		if (frames_encontrados != framesNecesarios) {
			return 0;
		}

		/* Asignar frames al proceso */

		int frame_inicial = i - framesNecesarios;		// Indice primer frame
		int frame_final = i;				// Indice ultimo frame
		int j; //Contador que me va a ir aumentando el numero de pag

		for (i = frame_inicial, j = 0; i < frame_final; i++, j++) {



			tablaAdministrativa[i].pid = pid;
			tablaAdministrativa[i].pag = pagInicioHeap + j;
		}

		return 1;


}

int ultimaPaginaDeProceso (int pid) {

	int i;
	int ultimaPagina = -1;
	for(i=0; i< MARCOS; i++) {
		if(tablaAdministrativa[i].pid == pid){
			ultimaPagina ++;
		}
	}

	return ultimaPagina;

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

char *ir_a_frame_cache(int indice) {
	if (indice >= ENTRADAS_CACHE) {
		return NULL;
	}
	return ((char (*) [MARCO_SIZE]) memoria_cache) [indice];
}

void liberar_frames(int PID) {
	int i;
	for (i = 0; i < MARCOS; i++) {
		if (tablaAdministrativa[i].pid == PID) {
			tablaAdministrativa[i].pid = FRAME_LIBRE;
		}
	}
}

// INTERFAZ DE USUARIO

void *interaccionMemoria(void * _) {

	struct comando {
			char *nombre;
			void (*funcion) (char *param);
		};

	struct comando comandos[] = {
		{ "retardo", configurarRetardo },
		{ "dump", dump},
		{ "flush", flush },
		{ "size", size},
		{ "limpiar", limpiar_pantalla},
		{ "opciones", imprimirOpcionesDeMemoria }
	};

	imprimirOpcionesDeMemoria();

	char input[100];

	while (1) {

		memset(input, 0, sizeof input);
		fgets(input, sizeof input, stdin);

		if (strlen(input) == 1) {
			continue;
		}

		remover_salto_linea(input);

		char *inputline = strdup(input); // Si no hago eso, string_trim se rompe
		string_trim_left(&inputline); // Elimino espacios a la izquierda

		char *cmd = NULL; // Comando
		char *save = NULL; // Apunta al primer caracter de los siguentes caracteres sin parsear
		char *delim = " ,"; // Separador

		// strtok_r(3) devuelve un token leido
		cmd = strtok_r(inputline, delim, &save); // Comando va a ser el primer token

		int i;
		// La division de los sizeof me calcula la cantidad de comandos
		for (i = 0; i < (sizeof comandos / sizeof *comandos); i++) {
			char *_cmd = comandos[i].nombre;
			if (strcmp(cmd, _cmd) == 0) {
				char *param = strdup(save); // Para no pasarle save por referencia
				comandos[i].funcion(param);
				break;
			}
		}
		if (i == (sizeof comandos / sizeof *comandos)) {
			logear_error("Error: %s no es un comando", false, cmd);
		}

		free(inputline);
	}

}

void imprimirOpcionesDeMemoria() {

	printf("\n--------------------\n"
			"BIENVENIDO A LA MEMORIA\n"
			"\n"
			"Lista de comandos: \n"
			"\n"
			"Configurar tiempo de retardo:\n"
			"retardo [RETARDO]\n"
			"\n"
			"Comando Dump:\n"
			"dump\n"
			"\n"
			"Flush, cambiar este mensaje:\n"
			"flush\n"
			"\n"
			"Size, cambiar este mensaje:\n"
			"size\n"
			"\n"
			"Limpiar mensajes:\n"
			"limpiar\n"
			"\n"
			"Mostrar opciones nuevamente:\n"
			"opciones\n"
			"\n");
}

void limpiar_pantalla() {
	printf("\033[H\033[J");
}

// MANEJO DE CLIENTES

void atenderKernel(int socketKernel) {
	int ret;
	while (1) {
		ret = kernel_processar_operacion(socketKernel);

		if (ret == -1) {
			cerrar_conexion(socketKernel, "Error de conexion con el Kernel (socket %d)");
			logear_error("Finalizando memoria debido a desconexión del Kernel", true);
			break;
		}

		if (ret == -2) {
			cerrar_conexion(socketKernel, "El Kernel hizo una operacion invalida (socket %d)");
			break;
		}
	}
}

void atenderCPU(int socketCPU) {
	int ret;
	while (1) {
		ret = cpu_processar_operacion(socketCPU);

		if (ret == -1) {
			cerrar_conexion(socketCPU, "Error de conexion con el CPU (socket %d)");
			break;
		}

		if (ret == -2) {
			cerrar_conexion(socketCPU, "El CPU hizo una operacion invalida (socket %d)");
			break;
		}
	}
}

void agregar_cliente(char identificador, int socketCliente) {
	if (existe_cliente(socketCliente)) {
		logear_error("No se puede agregar 2 veces mismo socket", false);
		return;
	}

	miCliente *cliente = malloc(sizeof (miCliente));
	cliente->identificador = identificador;
	cliente->socketCliente = socketCliente;

	list_add(clientes, cliente);
}

void borrar_cliente(int socketCliente) {
	DEF_MISMO_SOCKET(socketCliente);
	list_remove_and_destroy_by_condition(clientes, mismoSocket, free);
}

void cerrar_conexion(int socketCliente, char* motivo) {
	logear_info(motivo, socketCliente);
	borrar_cliente(socketCliente); // Si el cliente no esta en la lista no hace nada
	close(socketCliente);
}

int existe_cliente(int socketCliente) {
	DEF_MISMO_SOCKET(socketCliente);
	return list_any_satisfy(clientes, mismoSocket);
}

int hayAlguienQueSea(char identificacion) {
	bool mismoID(void* elemento) {
		return identificacion == ((miCliente*) elemento)->identificador;
	}
	return list_any_satisfy(clientes, mismoID);
}

int recibir_handshake(int socket) {
	headerDeLosRipeados header;
	int bytesRecibidos = recibir_header(socket, &header);
	if (bytesRecibidos <= 0) {
		if (bytesRecibidos == -1) {
			cerrar_conexion(socket, "El socket %d se desconectó");
		}
		else {
			cerrar_conexion(socket, "Socket %d: Error en el recv");
		}
		return -1;
	}
	char codOp = header.codigoDeOperacion;
	return (codOp == KERNEL || codOp == CPU) ? codOp : -1;
}

int tipo_cliente(int socketCliente) {
	DEF_MISMO_SOCKET(socketCliente);
	miCliente *found = (miCliente*)(list_find(clientes, mismoSocket));
	if (found == NULL) {
		return -1;
	}
	return found->identificador;
}

// EXTRAS
int solo_numeros(char *str) {
	    while (*str) {
	        if (isdigit(*str++) == 0) {
	        	return 0;
	        }
	    }
	    return 1;
	}

void configurarRetardo(char * retardo) {

	string_trim(&retardo);

	if (strlen(retardo) == 0) {
				logear_error("El comando \"retardo\" recibe un parametro [RETARDO]", false);
				free(retardo);
				return;
			}

	if (solo_numeros(retardo) == 0) {
		printf("Debe ingresar un valor numerico");
		return;
	}

	int exRETARDO;

	exRETARDO = RETARDO;
	RETARDO = atoi(retardo);
	logear_info(
			"Comando de configuracion de retardo ejecutado. Fue cambiado de %i ms a %i ms\n",
			exRETARDO, RETARDO);

}

void dump(char *param) {
	string_trim(&param);



	if(!strcmp(param,"estructuramemoria")){

	char tmp[16];

	int pag_digits;			// Cantidad de digitos hexa de una pagina
	int offset_digits;		// Cantidad de digitos hexa de un desplazamiento

	snprintf(tmp, sizeof tmp - 1, "%x", MARCOS);
	pag_digits = strlen(tmp);

	snprintf(tmp, sizeof tmp - 1, "%x", MARCO_SIZE);
	offset_digits = strlen(tmp);

	int i_pag, i_offset;	// Indice pagina y desplazamiento
	for (i_pag = 0; i_pag < MARCOS; i_pag++) {

		char *frame = ir_a_frame(i_pag);

		for (i_offset = 0; i_offset < MARCO_SIZE; i_offset += 16) {
			printf("%0*X:%0*X ", pag_digits, i_pag, offset_digits, i_offset);
			fflush(stdout);

			int max_bytes = ((MARCO_SIZE - i_offset) > 16) ? 16 : (MARCO_SIZE - i_offset);

			int i_byte;
			for (i_byte = 0; i_byte < max_bytes; i_byte++) {
				printf("%02X", (unsigned char) frame[i_offset + i_byte]);
				if (i_byte % 2 == 1) {
					printf(" ");
				}
				fflush(stdout);
			}

			for (i_byte = 0; i_byte < max_bytes; i_byte++) {
				if (iscntrl(frame[i_offset + i_byte])) {
					printf(".");
				}
				else {
					printf("%c", frame[i_offset + i_byte]);
				}
				fflush(stdout);
			}

			printf("\n");
			fflush(stdout);
		}
	}

	}else if(!strcmp(param,"contenidomemoria"))
	{
		// TODO
	}else if(!strcmp(param,"cache")){
		// TODO
	}else{
		logear_error("Comando invalido", false);
	}

	free(param);
}

void establecer_configuracion() {
	if (config_has_property(config, "PUERTO")) {
		PUERTO = config_get_int_value(config, "PUERTO");
		logear_info("PUERTO: %i", PUERTO);
	}
	else {
		logear_error("Error al leer el puerto de la memoria", true);
	}

	if (config_has_property(config, "MARCOS")) {
		MARCOS = config_get_int_value(config, "MARCOS");
		logear_info("MARCOS: %i", MARCOS);
	}
	else {
		logear_error("Error al leer los marcos de la memoria", true);
	}

	if (config_has_property(config, "MARCO_SIZE")) {
		MARCO_SIZE = config_get_int_value(config, "MARCO_SIZE");
		logear_info("MARCO_SIZE: %i", MARCO_SIZE);
	}
	else {
		logear_error("Error al leer los tamaños de los marcos de la memoria",
		true);
	}

	if (config_has_property(config, "ENTRADAS_CACHE")) {
		ENTRADAS_CACHE = config_get_int_value(config, "ENTRADAS_CACHE");
		logear_info("ENTRADAS_CACHE: %i", ENTRADAS_CACHE);
	}
	else {
		logear_error("Error al leer las entradas cache de la memoria", true);
	}

	if (config_has_property(config, "CACHE_X_PROC")) {
		ENTRADAS_CACHE = config_get_int_value(config, "CACHE_X_PROC");
		logear_info("CACHE_X_PROC: %i", CACHE_X_PROC);
	}
	else {
		logear_error("Error al leer los cache por proceso de la memoria", true);
	}

	if (config_has_property(config, "REEMPLAZO_CACHE")) {
		strcpy(REEMPLAZO_CACHE,
				config_get_string_value(config, "REEMPLAZO_CACHE"));
		logear_info("REEMPLAZO_CACHE: %s", REEMPLAZO_CACHE);
	}
	else {
		logear_error("Error al leer los reemplazo cache de la memoria", true);
	}

	if (config_has_property(config, "RETARDO")) {
		RETARDO = config_get_int_value(config, "RETARDO");
		logear_info("RETARDO: %i", RETARDO);
	}
	else {
		logear_error("Error al leer el retardo de la memoria", true);
	}
}

void finalizarPrograma(int numCliente, unsigned short payload) {
	// payload "identificador del programa".
	int PID;
	recv(numCliente, &PID, sizeof(PID), 0);
	// eliminar entradas en la estructura.

}

void flush() {
	int i;
	for (i = 0; i < ENTRADAS_CACHE; i++) {
		tablaAdministrativa_cache[i].pid = FRAME_LIBRE;
	}
}

void size(char *param) {
	string_trim(&param);

	if (!strcmp(param, "memory")) {
		printf("Cantidad de Frames de la memoria: %i\n", MARCOS);
		int framesLibres = 0, i;
		for (i = 0; i < MARCOS; i++) {
			if (tablaAdministrativa[i].pid == FRAME_LIBRE) {
				framesLibres++;
			}
		}
		printf("Cantidad de frames libres %i\n", framesLibres);
		printf("Cantidad de frames ocupados %i\n", MARCOS - framesLibres);
	}

	else {
		if (!strncmp(param, "pid ", 4)) {
			int pid;
			char *pid_c = malloc(3);
			pid_c = string_substring_from(param, 4);
			pid = atoi(pid_c);

			int i;
			int frames_usados = 0;
			for (i = 0; i < MARCOS; i++) {
				if (tablaAdministrativa[i].pid == pid)
					frames_usados++;
			}

			if (frames_usados == 0) {
				printf("No existe el proceso\n");
			}

			else {
				printf("Tamanio total del proceso (PID = %i) = %i Bytes\n", pid,
						frames_usados * MARCO_SIZE);
			}
		}

		else {
			logear_error("Comando invalido", false);
		}
	}
	free(param);
}

char* remover_salto_linea(char* s) { // By Beej
    int len = strlen(s);

    if (len > 0 && s[len-1] == '\n')  // if there's a newline
        s[len-1] = '\0';          // truncate the string

    return s;
}
