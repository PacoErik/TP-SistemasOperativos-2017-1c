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
#include "memoria.h"
#include "qepd/qepd.h"
#include "op_cpu.h"
#include "op_kernel.h"

/*----------DEFINES----------*/
#define DIVIDE_ROUNDUP(x,y) ((x - 1) / y + 1)

#define FRAME_ADMIN -1
#define FRAME_LIBRE -2

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

char *memoria;
char *memoria_cache;

estructura_administrativa_cache *tabla_administrativa_cache;
estructura_administrativa *tabla_administrativa;

int socket_kernel;

/*--------PROCEDIMIENTO PRINCIPAL----------*/

int main(void) {
	socket_kernel = 0;

	configurar("memoria");

	crear_memoria(); // Creacion de la memoria general y cache

	inicializar_tabla();
	inicializar_tabla_cache();

	pthread_t hilo_consola;
	pthread_create(&hilo_consola, NULL, interaccion_memoria, NULL);

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
		int nuevo_cliente;					// Socket del nuevo cliente conectado

		struct sockaddr_in cliente_info;
		socklen_t addrlen = sizeof cliente_info;

		nuevo_cliente = accept(servidor, (struct sockaddr *) &cliente_info, &addrlen);

		if (nuevo_cliente == -1) {
			logear_error("Fallo en el accept", false);
		}

		int *param = malloc(sizeof(int));
		*param = nuevo_cliente;

		pthread_t tid;						// Identificador del hilo
		pthread_attr_t atributos;			// Atributos del hilo

		pthread_attr_init(&atributos);
		pthread_attr_setdetachstate(&atributos, PTHREAD_CREATE_DETACHED);	// Detachable

		pthread_create(&tid, &atributos, &atender_cliente, param);

		pthread_attr_destroy(&atributos);

		logear_info("-Nueva conexión desde %s en el socket %d-",
				inet_ntoa(cliente_info.sin_addr), nuevo_cliente);
	}
}

/*----------DEFINICIÓN DE INTERFAZ DE MEMORIA---------*/

int memoria_inicializar_programa(int PID, int paginas_requeridas) {
	usleep(RETARDO * 1000);

	int paginas_disponibles = 0;
	int i = 0;

	while (paginas_disponibles < paginas_requeridas && i < MARCOS) {
		estructura_administrativa estructura = tabla_administrativa[i];
		if (estructura.pid == FRAME_LIBRE) {
			paginas_disponibles++;
		}
		i++;
	}

	if (paginas_disponibles == paginas_requeridas) {
		return true;
	}
	return NO_SE_PUDIERON_RESERVAR_RECURSOS;
}
char* memoria_solicitar_bytes(int PID, int numero_pagina, int offset, int size) {
	//////////////////////
	//Resolver por caché//
	//////////////////////

	usleep(RETARDO * 1000);

	int frame = traducir_a_frame(numero_pagina, PID);
	if (frame >= 0) {
		return ir_a_frame(frame)+offset;
	}
	return NULL;
}
int memoria_almacenar_bytes(int PID, int numero_pagina, int offset, int size, void *buffer) {
	//////////////////////
	//Resolver por caché//
	//////////////////////

	usleep(RETARDO * 1000);

	int frame = traducir_a_frame(numero_pagina, PID);
	if (frame >= 0) {
		memcpy(ir_a_frame(frame)+offset, buffer, size);
		return true;
	}
	return FALLO_DE_SEGMENTO;
}
int memoria_asignar_paginas(int PID, int paginas_requeridas) {
	usleep(RETARDO * 1000);

	int paginas_asignadas = 0;
	int i = 0;
	int ultima_pagina = ultima_pagina_proceso(PID);

	while (paginas_asignadas < paginas_requeridas && i < MARCOS) {
		if (tabla_administrativa[i].pid == FRAME_LIBRE) {
			ultima_pagina++;
			tabla_administrativa[i].frame = i;
			tabla_administrativa[i].pag = ultima_pagina;
			tabla_administrativa[i].pid = PID;
			paginas_asignadas++;
		}
		i++;
	}

	if (paginas_asignadas == paginas_requeridas) {
		return true;
	}
	return NO_SE_PUEDEN_ASIGNAR_MAS_PAGINAS;
}
int memoria_liberar_pagina(int PID, int numero_pagina) {
	usleep(RETARDO * 1000);

	int frame = traducir_a_frame(numero_pagina, PID);
	estructura_administrativa entrada = tabla_administrativa[frame];
	if (entrada.pid == PID && entrada.pag == numero_pagina) {
		entrada.pid = FRAME_LIBRE;
		return true;
	}

	return EXCEPCION_MEMORIA; //Esto jamás debería pasar
}
int memoria_finalizar_programa(int PID) {
	usleep(RETARDO * 1000);

	int i;
	for (i = 0; i < MARCOS; i++) {
		if (tabla_administrativa[i].pid == PID) {
			tabla_administrativa[i].pid = FRAME_LIBRE;
		}
	}

	return 1;
}
int memoria_handshake(int socket) {
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

/*------------DEFINICION DE FUNCIONES AUXILIARES----------------*/

// FUNCION DEL HILO

void *atender_cliente(void* param) {
	int socket_cliente = (int)*((int*)param);
	int tipoCliente = memoria_handshake(socket_cliente);
	if (tipoCliente == -1) {

		cerrar_conexion(socket_cliente, "Socket %d: Operacion Invalida");
		return NULL;

	}
	if (tipoCliente == KERNEL) {

		if (socket_kernel > 0) {
			cerrar_conexion(socket_cliente, "El cliente %i intentó conectarse como Kernel ya habiendo uno");
			return NULL;
		}

		socket_kernel = socket_cliente;
		logear_info("-Kernel conectado-");
		send(socket_cliente, "Bienvenido!", sizeof "Bienvenido!", 0);
		send(socket_cliente, &MARCO_SIZE, sizeof(int), 0);
		atender_kernel();

	}
	else {

		logear_info("-Nuevo CPU conectado-");
		send(socket_cliente, "Bienvenido!", sizeof "Bienvenido!", 0);
		send(socket_cliente, &MARCO_SIZE, sizeof(int), 0);
		atender_CPU(socket_cliente);

	}
	return NULL;
}

// MANEJO DE MEMORIA

void crear_memoria(void) {
	memoria = calloc(MARCOS , MARCO_SIZE);

	memoria_cache = calloc(ENTRADAS_CACHE , MARCO_SIZE);
}
int max_LRU(void){
	int i, max = 0;

	for (i = 0; i < ENTRADAS_CACHE; i++) {
		if (tabla_administrativa_cache[i].lru > max)
			max = tabla_administrativa_cache[i].lru;
	}

	return max;
}
int posicion_a_reemplazar_cache(void){
	int i, min = 1000, pos = 0;

	for (i = 0; i < ENTRADAS_CACHE; i++) {
		if (tabla_administrativa_cache[i].pid == FRAME_LIBRE)
			return i;
	}

	for (i = 0; i < ENTRADAS_CACHE; i++) {
		if (tabla_administrativa_cache[i].lru < min) {
			min = tabla_administrativa_cache[i].lru;
			pos = i;
		}
	}

	return pos;
}
void inicializar_tabla(void) {
	// Aca no necesita ningun malloc, se guarda directamente en memoria
	tabla_administrativa = (estructura_administrativa *) memoria;

	int tamanio_total_tabla = sizeof(estructura_administrativa[MARCOS]);
	int frames_ocupados_por_tabla = DIVIDE_ROUNDUP(tamanio_total_tabla, MARCO_SIZE);

	int i;
	for (i = 0 ; i < frames_ocupados_por_tabla; i++) {
		tabla_administrativa[i].pid = FRAME_ADMIN;
	}

	for ( ; i < MARCOS; i++) {
		tabla_administrativa[i].pid = FRAME_LIBRE;
	}
}

/*
int buscar_en_memoria_y_devolver(posicion_memoria_variable posicion, int socket) {

	int pos_main_memory;

	//Recorro la tabla admin de la cache para ver si esta
	for (pos_main_memory = 0; pos_main_memory < ENTRADAS_CACHE; pos_main_memory++) {
		if (tabla_administrativa_cache[pos_main_memory].pid == posicion.pid
				&& tabla_administrativa_cache[pos_main_memory].pag
						== posicion.numero_pagina) {

			enviar_desde_cache(pos_main_memory, socket, posicion);

			tabla_administrativa_cache[pos_main_memory].lru = max_LRU() + 1; // en la posicion i de la tabla de cache,correspondiente al pid encontrado, se le asigna al campo lru el maximo lru encontrado + 1

			return 1;
		}
	}

	usleep(RETARDO * 1000);

	// Recorro la tabla admin de la memoria principal
	for (pos_main_memory = 0; pos_main_memory < MARCOS; pos_main_memory++) {
		if (tabla_administrativa[pos_main_memory].pid == posicion.pid
				&& tabla_administrativa[pos_main_memory].pag
						== posicion.numero_pagina) {

			//enviarDesdeMemoria();

			int pos_cache;
			pos_cache = posicion_a_reemplazar_cache();

			tabla_administrativa_cache[pos_cache].pid = posicion.pid;
			tabla_administrativa_cache[pos_cache].pag = posicion.numero_pagina;
			tabla_administrativa_cache[pos_cache].lru = max_LRU() + 1;

			// Se copia a memoria cache

			memcpy(
					memoria_cache
							+ tabla_administrativa_cache[pos_cache].contenido_pagina,
					memoria + MARCO_SIZE * pos_main_memory, MARCO_SIZE);

			return 1;

		}
	}

	return 0;

}
*/

void inicializar_tabla_cache(void) {

	int i;

	tabla_administrativa_cache = malloc(ENTRADAS_CACHE * sizeof(estructura_administrativa_cache));

	for (i=0 ; i < ENTRADAS_CACHE; i++) {
			tabla_administrativa_cache[i].pid = FRAME_LIBRE;
			//La primer pag en cache empieza en 0 * MARCO_SIZE = 0, las sig posiciones de inicio se mueven de a MARCO_SIZE, por eso el i*MARCO_SIZE
			tabla_administrativa_cache[i].contenido_pagina = i * MARCO_SIZE;
		}

}
int ultima_pagina_proceso (int pid) {

	int i;
	int ultima_pagina = -1;
	for(i=0; i< MARCOS; i++) {
		estructura_administrativa entrada = tabla_administrativa[i];
		if(entrada.pid == pid && entrada.pag > ultima_pagina){
			ultima_pagina = entrada.pag;
		}
	}

	return ultima_pagina;

}
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
		if (tabla_administrativa[i].pid == PID) {
			tabla_administrativa[i].pid = FRAME_LIBRE;
		}
	}
}
int traducir_a_frame(int pagina, int pid) {
	//la forma más básica de traducir a frame
	//en realidad habría que usar la función de hashing
	int i;
	for (i = 0; i < MARCOS; i++) {
		if (tabla_administrativa[i].pid == pid
			&& tabla_administrativa[i].pag == pagina) {
			return i;
		}
	}
	return -1;
}

// INTERFAZ DE USUARIO

void configurar_retardo(char * retardo) {

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
void flush() {
	int i;
	for (i = 0; i < ENTRADAS_CACHE; i++) {
		tabla_administrativa_cache[i].pid = FRAME_LIBRE;
	}
}
void *interaccion_memoria(void * _) {

	struct comando {
			char *nombre;
			void (*funcion) (char *param);
		};

	struct comando comandos[] = {
		{ "retardo", configurar_retardo },
		{ "dump", dump},
		{ "flush", flush },
		{ "size", size},
		{ "limpiar", limpiar_pantalla},
		{ "opciones", imprimir_opciones_memoria }
	};

	imprimir_opciones_memoria();

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
void imprimir_opciones_memoria() {

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
void size(char *param) {
	string_trim(&param);

	if (!strcmp(param, "memory")) {
		printf("Cantidad de Frames de la memoria: %i\n", MARCOS);
		int frames_libres = 0, i;
		for (i = 0; i < MARCOS; i++) {
			if (tabla_administrativa[i].pid == FRAME_LIBRE) {
				frames_libres++;
			}
		}
		printf("Cantidad de frames libres %i\n", frames_libres);
		printf("Cantidad de frames ocupados %i\n", MARCOS - frames_libres);
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
				if (tabla_administrativa[i].pid == pid)
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

// MANEJO DE CLIENTES

void atender_kernel() {
	int ret;
	while (1) {
		ret = kernel_procesar_operacion();

		if (ret == -1) {
			cerrar_conexion(socket_kernel, "Error de conexion con el Kernel (socket %d)");
			logear_error("Finalizando memoria debido a desconexión del Kernel", true);
			break;
		}

		if (ret == -2) {
			cerrar_conexion(socket_kernel, "El Kernel hizo una operacion invalida (socket %d)");
			break;
		}
	}
}
void atender_CPU(int socket_CPU) {
	int ret;
	while (1) {
		ret = cpu_procesar_operacion(socket_CPU);

		if (ret == -1) {
			cerrar_conexion(socket_CPU, "Error de conexion con el CPU (socket %d)");
			break;
		}

		if (ret == -2) {
			cerrar_conexion(socket_CPU, "El CPU hizo una operacion invalida (socket %d)");
			break;
		}
	}
}
void cerrar_conexion(int socketCliente, char* motivo) {
	logear_info(motivo, socketCliente);
	close(socketCliente);
}
void enviar_excepcion(int socket_cliente, int excepcion) {
	enviar_header(socket_cliente, EXCEPCION, sizeof(excepcion));
	send(socket_cliente, &excepcion, sizeof(excepcion), 0);
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
char* remover_salto_linea(char* s) { // By Beej
    int len = strlen(s);

    if (len > 0 && s[len-1] == '\n')  // if there's a newline
        s[len-1] = '\0';          // truncate the string

    return s;
}
