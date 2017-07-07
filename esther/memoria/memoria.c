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
#include <sys/timeb.h>
#include "commons/collections/list.h"
#include "memoria.h"
#include "qepd/qepd.h"
#include "op_cpu.h"
#include "op_kernel.h"

/*----------DEFINES----------*/
#define DIVIDE_ROUNDUP(x,y) ((x - 1) / y + 1)
#define FRAME_ADMIN -1
#define FRAME_LIBRE -2
#define DUMP_PRINT(file, format, ...) {				\
	printf(format, ##__VA_ARGS__);					\
	fprintf(file, format, ##__VA_ARGS__);			\
}

/*-----VARIABLES GLOBALES-----*/

/* Configs */
int					PUERTO;
short int			RETARDO;
int					MARCOS;
int					MARCO_SIZE;
int					ENTRADAS_CACHE;
int					CACHE_X_PROC;
char				REEMPLAZO_CACHE[8]; // Useless variable is useless.

t_log* logger;
t_config* config;

char *memoria;
char *memoria_cache;
t_list **overflow;

estructura_administrativa_cache *tabla_administrativa_cache;
estructura_administrativa *tabla_administrativa;

int socket_kernel;

pthread_mutex_t mutex_cache;
pthread_mutex_t *acceso_hash;

/*--------PROCEDIMIENTO PRINCIPAL----------*/

int main(void) {
	socket_kernel = 0;

	configurar("memoria");

	crear_memoria(); // Creacion de la memoria general y cache

	inicializar_tabla();
	inicializar_tabla_cache();

	hash_iniciar_overflow();

	pthread_mutex_init(&mutex_cache, NULL);

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

	for (;;) {
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
	int frame = traducir_a_frame(numero_pagina, PID);
	if (frame < 0)
		return NULL;
	if (offset + size > MARCO_SIZE)
		return NULL;

	/////////////////////
	//Caché intensifies//
	if (CACHE_X_PROC > 0 && ENTRADAS_CACHE > 0) {
		char *buffer = cache_solicitar_bytes(PID, numero_pagina, frame, offset);
		if (buffer != NULL)
			return buffer;
	}
	/////////////////////

	usleep(RETARDO * 1000);

	return ir_a_frame_memoria(frame) + offset;
}
int memoria_almacenar_bytes(int PID, int numero_pagina, int offset, int size, void *buffer) {
	int frame = traducir_a_frame(numero_pagina, PID);
	if (frame < 0)
		return FALLO_DE_SEGMENTO;
	if (offset + size > MARCO_SIZE)
		return FALLO_DE_SEGMENTO;

	/////////////////////
	//Caché intensifies//
	if (CACHE_X_PROC > 0 && ENTRADAS_CACHE > 0) {
		int respuesta = cache_almacenar_bytes(PID, numero_pagina, frame, offset, size, buffer);
		if (respuesta > 0)
			return true;
	}
	/////////////////////

	usleep(RETARDO * 1000);

	memcpy(ir_a_frame_memoria(frame) + offset, buffer, size);
	return true;
}

int memoria_asignar_paginas(int PID, int paginas_requeridas) {
	usleep(RETARDO * 1000);

	int i;
	int ultima_pagina = ultima_pagina_proceso(PID);

	int posicion;
	int frame;

	for (i = 0; i < paginas_requeridas; i++) {
		posicion = hash_calcular_posicion(PID, ultima_pagina + i + 1);

		if (tabla_administrativa[posicion].pid == FRAME_LIBRE) {
			frame = posicion;
		}

		/* Hubo colision */
		else {
			frame = proximo_frame_libre(posicion);

			if (frame == -1) {
				return NO_SE_PUEDEN_ASIGNAR_MAS_PAGINAS;
			}
		}

		hash_agregar_en_overflow(posicion, frame);
		logear_info("[PID:%d] Página %d asignada al frame %d", PID, ultima_pagina + i + 1, frame);
		tabla_administrativa[frame].pid = PID;
		tabla_administrativa[frame].pag = ultima_pagina + i + 1;
	}

	return true;
}

int memoria_liberar_pagina(int PID, int numero_pagina) {
	usleep(RETARDO * 1000);

	int frame = traducir_a_frame(numero_pagina, PID);
	if (frame == -1) {
		return EXCEPCION_MEMORIA;
	}

	//Borrar página de memoria caché
	int i = 0;
	while (i < ENTRADAS_CACHE) {
		if (tabla_administrativa_cache[i].pid == PID
				&& tabla_administrativa_cache[i].pag == numero_pagina) {
			tabla_administrativa_cache[i].pid = FRAME_LIBRE;
			tabla_administrativa_cache[i].lru = 0;
			break;
		}
		i++;
	}

	//Borrar página de memoria
	tabla_administrativa[frame].pid = FRAME_LIBRE;
	hash_borrar_de_overflow(hash_calcular_posicion(PID, numero_pagina), frame);

	return true;
}
int memoria_finalizar_programa(int PID) {
	usleep(RETARDO * 1000);

	int i;

	for (i = 0; i < MARCOS; i++) {
		if (tabla_administrativa[i].pid == PID) {
			tabla_administrativa[i].pid = FRAME_LIBRE;
			hash_borrar_de_overflow(hash_calcular_posicion(PID, tabla_administrativa[i].pag), i);
		}
	}

	pthread_mutex_lock(&mutex_cache);
	for (i = 0; i < ENTRADAS_CACHE; i++) {
		if (tabla_administrativa_cache[i].pid == PID) {
			tabla_administrativa_cache[i].pid = FRAME_LIBRE;
			tabla_administrativa_cache[i].lru = 0;
		}
	}
	pthread_mutex_unlock(&mutex_cache);

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

/*----------DEFINICIÓN DE INTERFAZ DE CACHÉ-----------*/

int cache_almacenar_bytes(int PID, int numero_pagina, int frame, int offset, int size, void *buffer) {
	pthread_mutex_lock(&mutex_cache);
	int contenido = cache_buscar_pagina(PID, numero_pagina);

	if (contenido < 0) {
		contenido = cache_almacenar_pagina(PID, numero_pagina, frame);
		memcpy(memoria_cache + contenido + offset, buffer, size);
		pthread_mutex_unlock(&mutex_cache);
		return false;
	}

	memcpy(ir_a_frame_memoria(frame) + offset, buffer, size);
	memcpy(memoria_cache + contenido + offset, buffer, size);

	pthread_mutex_unlock(&mutex_cache);
	return true;
}
int cache_almacenar_pagina(int PID, int numero_pagina, int frame) {
	int cantidad_paginas_cache_proceso = 0;
	int i;
	estructura_administrativa_cache *entrada;

	Victima victima_global;
	victima_global.lru = max_LRU() + 1;
	Victima victima_local = victima_global;
	Victima victima_definitiva = victima_global;

	for (i = 0; i < ENTRADAS_CACHE; i++) {
		entrada = cache_obtener_entrada(i);

		//Lógica de la víctima local (propia del proceso)
		if (entrada->pid == PID) {
			cantidad_paginas_cache_proceso++;
			if (entrada->lru < victima_local.lru) {
				victima_local.indice = i;
				victima_local.lru = entrada->lru;
			}
		}

		//Lógica de la víctima global (de cualquier proceso)
		if (entrada->lru < victima_global.lru) {
			victima_global.indice = i;
			victima_global.lru = entrada->lru;
		}

	}

	if (cantidad_paginas_cache_proceso == CACHE_X_PROC) {
		victima_definitiva.indice = victima_local.indice;
	}
	else {
		victima_definitiva.indice = victima_global.indice;
	}

	entrada = cache_obtener_entrada(victima_definitiva.indice);

	if (entrada->pid == FRAME_LIBRE) {
		logear_info(
				"[Caché MISS - Entrada %d] Página libre reemplazada por (Pág:%d) de (PID:%d)",
				victima_definitiva.indice, numero_pagina, PID);
	}
	else if (entrada->pid == PID) {
		logear_info(
				"[Caché MISS - Entrada %d] (Pág:%d) del mismo proceso (PID:%d) reemplazada por (Pág:%d)",
				victima_definitiva.indice, entrada->pag, PID, numero_pagina);
	}
	else {
		logear_info(
				"[Caché MISS - Entrada %d] (Pág:%d) del proceso (PID:%d) reemplazada por (Pág:%d) de (PID:%d)",
				victima_definitiva.indice, entrada->pag, entrada->pid,
				numero_pagina, PID);
	}

	entrada->pid = PID;
	entrada->lru = victima_definitiva.lru;
	entrada->pag = numero_pagina;

	memcpy(memoria_cache + entrada->contenido, ir_a_frame_memoria(frame), MARCO_SIZE);

	return entrada->contenido;
}
int cache_buscar_pagina(int PID, int numero_pagina) {
	int i;
	for (i = 0; i < ENTRADAS_CACHE; i++) {
		estructura_administrativa_cache *entrada = cache_obtener_entrada(i);
		if (entrada->pid == PID && entrada->pag == numero_pagina) {
			entrada->lru = max_LRU() + 1;
			return entrada->contenido;
		}
	}

	return -1;
}
estructura_administrativa_cache *cache_obtener_entrada(int indice) {
	char *p = (char*) tabla_administrativa_cache;
	return (estructura_administrativa_cache*) (p + indice * sizeof(estructura_administrativa_cache));
}
char *cache_solicitar_bytes(int PID, int numero_pagina, int frame, int offset) {
	pthread_mutex_lock(&mutex_cache);
	int contenido = cache_buscar_pagina(PID, numero_pagina);

	if (contenido < 0) {
		cache_almacenar_pagina(PID, numero_pagina, frame);
		pthread_mutex_unlock(&mutex_cache);
		return NULL;
	}

	pthread_mutex_unlock(&mutex_cache);

	return memoria_cache + contenido + offset;
}

/*------------HASHING STUFF---------------------------*/

void hash_agregar_en_overflow(int posicion, int frame) {
	int *frame_p = malloc(sizeof(int));
	*frame_p = frame;
	pthread_mutex_lock(&acceso_hash[posicion]);
	list_add(overflow[posicion], frame_p);
	pthread_mutex_unlock(&acceso_hash[posicion]);
}
void hash_borrar_de_overflow(int posicion, int frame) {
	_Bool mismo_frame(void *otro_frame) {
		return *(int*) otro_frame == frame;
	}
	pthread_mutex_lock(&acceso_hash[posicion]);
	list_remove_and_destroy_by_condition(overflow[posicion], &mismo_frame, free);
	pthread_mutex_unlock(&acceso_hash[posicion]);
}
int hash_buscar_en_overflow(int pid, int pagina) {
	int posicion = hash_calcular_posicion(pid, pagina);

	bool _es_frame_correcto(void *frame) {
		return hash_pagina_correcta(*((int *)frame), pid, pagina);
	}

	pthread_mutex_lock(&acceso_hash[posicion]);
	int *frame = (int *) list_find(overflow[posicion], _es_frame_correcto);
	pthread_mutex_unlock(&acceso_hash[posicion]);

	return frame == NULL ? -1 : *frame;
}
int hash_calcular_posicion(int pid, int num_pagina) {
	int frames_ocupados_por_tabla = DIVIDE_ROUNDUP(
			sizeof(estructura_administrativa[MARCOS]), MARCO_SIZE);

	/* Saltear los primeros frames ocupados por tabla para no colisionar con ellos */

	return frames_ocupados_por_tabla
				+ (pid + (MARCOS / 10) * num_pagina + num_pagina * num_pagina)
					% (MARCOS - frames_ocupados_por_tabla);
}
void hash_iniciar_overflow(void) {
	overflow = malloc(sizeof(t_list*) * MARCOS);
	acceso_hash = malloc(MARCOS * sizeof(pthread_mutex_t));

	int posicion;
	for (posicion = 0; posicion < MARCOS; posicion++) {
		overflow[posicion] = list_create();
		pthread_mutex_init(&acceso_hash[posicion], NULL);
	}
}
_Bool hash_pagina_correcta(int pos_candidata, int pid, int pagina) {
	return tabla_administrativa[pos_candidata].pid == pid
				&& tabla_administrativa[pos_candidata].pag == pagina;
}

/*------------DEFINICION DE FUNCIONES AUXILIARES----------------*/

// FUNCION DEL HILO
void *atender_cliente(void* param) {
	int socket_cliente = (int) *((int*) param);
	free(param);

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

int proximo_frame_libre(int indice) {
	int i;

	int tamanio_total_tabla = sizeof(estructura_administrativa[MARCOS]); //El buen copypaste
	int frames_ocupados_por_tabla = DIVIDE_ROUNDUP(tamanio_total_tabla, MARCO_SIZE);

	for (i = indice + 1; i < MARCOS; i++) {
		if (tabla_administrativa[i].pid == FRAME_LIBRE) {
			return i;
		}
	}

	for (i = indice - 1; i >= frames_ocupados_por_tabla; i--) {
		if (tabla_administrativa[i].pid == FRAME_LIBRE) {
			return i;
		}
	}
	//pa' delante, pa' atra'

	return -1;
}
void crear_memoria(void) {
	memoria = calloc(MARCOS, MARCO_SIZE);

	memoria_cache = calloc(ENTRADAS_CACHE, MARCO_SIZE);
}
int max_LRU(void) {
	int i, max = 0;

	for (i = 0; i < ENTRADAS_CACHE; i++) {
		if (tabla_administrativa_cache[i].lru > max)
			max = tabla_administrativa_cache[i].lru;
	}

	return max;
}
void inicializar_tabla(void) {
	// Aca no necesita ningun malloc, se guarda directamente en memoria
	tabla_administrativa = (estructura_administrativa *) memoria;

	int tamanio_total_tabla = sizeof(estructura_administrativa[MARCOS]);
	int frames_ocupados_por_tabla = DIVIDE_ROUNDUP(tamanio_total_tabla, MARCO_SIZE);

	if (frames_ocupados_por_tabla >= MARCOS) {
		logear_error("[Fail] La cantidad de frames de la tabla administrativa superó la cantidad de frames de memoria, finalizando...", true);
		//c mamó
	} //c mamó feo

	int i;
	for (i = 0; i < frames_ocupados_por_tabla; i++) {
		tabla_administrativa[i].pid = FRAME_ADMIN;
	}

	for (; i < MARCOS; i++) {
		tabla_administrativa[i].pid = FRAME_LIBRE;
	}
}
void inicializar_tabla_cache(void) {
	int i;

	tabla_administrativa_cache = calloc(ENTRADAS_CACHE, sizeof(estructura_administrativa_cache));
	for (i = 0; i < ENTRADAS_CACHE; i++) {
		tabla_administrativa_cache[i].pid = FRAME_LIBRE;
		tabla_administrativa_cache[i].lru = 0;
		tabla_administrativa_cache[i].contenido = i * MARCO_SIZE;
	}

}
int ultima_pagina_proceso(int pid) {

	int i;
	int ultima_pagina = -1;
	for (i = 0; i < MARCOS; i++) {
		estructura_administrativa entrada = tabla_administrativa[i];
		if (entrada.pid == pid && entrada.pag > ultima_pagina) {
			ultima_pagina = entrada.pag;
		}
	}

	return ultima_pagina;

}
char *ir_a_frame_memoria(int indice) {
	if (indice >= MARCOS) {
		return NULL;
	}
	return ((char (*)[MARCO_SIZE]) memoria)[indice];
}
char *ir_a_frame_cache(int indice) {
	if (indice >= ENTRADAS_CACHE) {
		return NULL;
	}
	return ((char (*)[MARCO_SIZE]) memoria_cache)[indice];
}
void liberar_frames(int PID) {
	int i;
	for (i = 0; i < MARCOS; i++) {
		if (tabla_administrativa[i].pid == PID) {
			tabla_administrativa[i].pid = FRAME_LIBRE;
		}
	}
}

int traducir_a_frame(int pagina, int PID) {
	return hash_buscar_en_overflow(PID, pagina);
}

// INTERFAZ DE USUARIO

void configurar_retardo(char *retardo) {
	string_trim(&retardo);

	if (strlen(retardo) == 0) {
		logear_error("El comando \"retardo\" recibe un parametro [RETARDO]", false);

		free(retardo);
		return;
	}

	if (solo_numeros(retardo) == 0) {
		logear_error("Debe ingresar un valor numerico.", false);

		free(retardo);
		return;
	}

	int exRETARDO;

	exRETARDO = RETARDO;
	RETARDO = atoi(retardo);
	free(retardo);

	logear_info(
			"Comando de configuracion de retardo ejecutado. Fue cambiado de %i ms a %i ms\n",
			exRETARDO, RETARDO);

}
void dump(char *param) {
	void _dump(FILE *output_file, int entries, int size, char *buffer) {
		char tmp[16];

		int pag_digits;			// Cantidad de digitos hexa de una pagina
		int offset_digits;		// Cantidad de digitos hexa de un desplazamiento

		snprintf(tmp, sizeof tmp - 1, "%x", entries);
		pag_digits = strlen(tmp);

		snprintf(tmp, sizeof tmp - 1, "%x", size);
		offset_digits = strlen(tmp);

		int i_pag, i_offset;	// Indice pagina y desplazamiento
		for (i_pag = 0; i_pag < entries; i_pag++) {
			char *frame = ((char (*)[size]) buffer)[i_pag];

			for (i_offset = 0; i_offset < size; i_offset += 16) {
				DUMP_PRINT(output_file, "%0*X:%0*X ", pag_digits, i_pag, offset_digits, i_offset);
				fflush(stdout);

				int max_bytes = (size - i_offset) > 16 ? 16 : (size - i_offset);

				hex_dump(output_file, max_bytes, &frame[i_offset]);
			}
		}
	}

	FILE *dump_file;
	char *dump_filename = strdup("dump_");
	char *timestamp = obtener_timestamp();

	string_trim(&param);

	if (!strcmp(param, "memoria")) {
		string_append(&dump_filename, "memoria_");
		string_append(&dump_filename, timestamp);
		string_append(&dump_filename, ".txt");

		dump_file = fopen(dump_filename, "w");
		_dump(dump_file, MARCOS, MARCO_SIZE, memoria);
		fclose(dump_file);
	}

	else if (!strcmp(param, "cache")) {
		string_append(&dump_filename, "cache_");
		string_append(&dump_filename, timestamp);
		string_append(&dump_filename, ".txt");

		dump_file = fopen(dump_filename, "w");

		_dump(dump_file, ENTRADAS_CACHE, MARCO_SIZE, memoria_cache);
		fclose(dump_file);
	}

	else if (!strncmp(param, "proceso ", sizeof "proceso " - 1)) {
		int pid;

		char *pid_c = string_substring_from(param, sizeof "proceso" - 1);
		string_trim(&pid_c);

		if (solo_numeros(pid_c)) {
			string_append(&dump_filename, "proceso_");
			string_append(&dump_filename, pid_c);
			string_append(&dump_filename, "_");
			string_append(&dump_filename, timestamp);
			string_append(&dump_filename, ".txt");

			dump_file = fopen(dump_filename, "w");

			pid = atoi(pid_c);

			char tmp[16];
			int offset_digits;		// Cantidad de digitos hexa de un desplazamiento

			snprintf(tmp, sizeof tmp - 1, "%x", MARCO_SIZE);
			offset_digits = strlen(tmp);

			DUMP_PRINT(dump_file, "Contenido PID %d:\n", pid);

			int i;
			int paginas;

			for (i = 0, paginas = 0; i < MARCOS; i++) {
				if (tabla_administrativa[i].pid == pid) {
					DUMP_PRINT(dump_file, "\nPagina %d (Frame %d)\n\n", tabla_administrativa[i].pag, i);

					int i_offset;
					char *frame = ir_a_frame_memoria(i);

					for (i_offset = 0; i_offset < MARCO_SIZE; i_offset += 16) {
						DUMP_PRINT(dump_file, "%0*X: ", offset_digits, i_offset);
						fflush(stdout);

						int max_bytes = (MARCO_SIZE - i_offset) > 16 ? 16 : (MARCO_SIZE - i_offset);
						hex_dump(dump_file, max_bytes, &frame[i_offset]);
					}

					paginas++;
				}
			}

			if (paginas == 0) {
				DUMP_PRINT(dump_file, "El proceso no existe.\n");
			}

			fclose(dump_file);
		}

		free(pid_c);
	}

	else if (!strcmp(param, "estructuras")) {
		string_append(&dump_filename, "estructuras_admin_");
		string_append(&dump_filename, timestamp);
		string_append(&dump_filename, ".txt");

		dump_file = fopen(dump_filename, "w");

		char tmp[16];
		int frame_digits;		// Cantidad de digitos de un frame

		snprintf(tmp, sizeof tmp - 1, "%d", MARCOS);
		frame_digits = strlen(tmp);

		DUMP_PRINT(dump_file, "┌───────────┬───────────┬───────────┐\n");
		DUMP_PRINT(dump_file, "│   Frame   │    PID    │   Pagina  │\n");
		DUMP_PRINT(dump_file, "├───────────┼───────────┼───────────┤\n");

		int i;
		t_list *pid_activos = list_create();

		for (i = 0; i < MARCOS; i++) {
			DUMP_PRINT(dump_file, "│%.*s", 10 - frame_digits, "          ");
			DUMP_PRINT(dump_file, "%0*d │", frame_digits, i);
			fflush(stdout);

			switch (tabla_administrativa[i].pid) {
			case FRAME_ADMIN:
				DUMP_PRINT(dump_file, "     ADMIN ");
				fflush(stdout);
				break;

			case FRAME_LIBRE:
				DUMP_PRINT(dump_file, "     LIBRE ");
				fflush(stdout);
				break;

			default:
				/* Agregarlo si no esta en la lista */
				if (!list_find(pid_activos, ({
						bool _eq_ (void *e)
							{ return *((int *) e) == tabla_administrativa[i].pid; }
						_eq_;
					}))) {
					int *p_pid = malloc(sizeof(int));
					*p_pid = tabla_administrativa[i].pid;

					list_add(pid_activos, p_pid);
				}
				DUMP_PRINT(dump_file, "     %5d", tabla_administrativa[i].pid);
				fflush(stdout);
				break;
			}

			if (tabla_administrativa[i].pid > FRAME_ADMIN) {
				DUMP_PRINT(dump_file, " │%.*s", 10 - frame_digits, "          ");
				DUMP_PRINT(dump_file, "%*d │", frame_digits, tabla_administrativa[i].pag);
				fflush(stdout);
			}

			else {
				DUMP_PRINT(dump_file, "│     -     │ ");
				fflush(stdout);
			}

			DUMP_PRINT(dump_file, "\n");
		}

		DUMP_PRINT(dump_file, "└───────────┴───────────┴───────────┘ \n");

		if (list_size(pid_activos) == 0) {
			DUMP_PRINT(dump_file, "No hay ningun proceso activo.\n");
			list_destroy(pid_activos);
		}

		else {
			DUMP_PRINT(dump_file, "PIDs Activos:\n");
			list_iterate(pid_activos, ({
				void _print_(void *e) {
					DUMP_PRINT(dump_file, "PID: %d\n", *((int *)e));
				}
				_print_;
			}));

			list_destroy_and_destroy_elements(pid_activos, free);
		}
		fclose(dump_file);
	}

	else {
		logear_error("Comando invalido", false);
	}

	free(dump_filename);
	free(timestamp);
	free(param);
}
void flush() {
	int i;
	pthread_mutex_lock(&mutex_cache);
	memset(memoria_cache, 0, ENTRADAS_CACHE * MARCO_SIZE);
	for (i = 0; i < ENTRADAS_CACHE; i++) {
		tabla_administrativa_cache[i].pid = FRAME_LIBRE;
		tabla_administrativa_cache[i].lru = 0;
	}
	logear_info("Contenido de la caché vaciado con éxito!");
	pthread_mutex_unlock(&mutex_cache);
}
void *interaccion_memoria(void * _) {
	struct comando {
		char *nombre;
		void (*funcion) (char *param);
	};

	struct comando comandos[] = {
		{ "retardo", configurar_retardo },
		{ "dump", dump },
		{ "flush", flush },
		{ "size", size },
		{ "limpiar", limpiar_pantalla },
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
			"Lista de comandos: \n"
			"retardo [Número de retardo] \t\t\t//Cambiar el retardo\n"
			"dump [memoria/cache/proceso/estructuras] \t//Generar volcado\n"
			"flush \t\t\t\t\t\t//Limpiar la caché\n"
			"size [memoria] \t\t\t\t\t//Mostrar tamaño de memoria\n"
			"size [PID] [Número de PID] \t\t\t//Mostrar tamaño de proceso\n"
			"limpiar \t\t\t\t\t//Limpiar pantalla\n"
			"opciones \t\t\t\t\t//Mostrar comandos\n"
			);
}
void limpiar_pantalla() {
	printf("\033[H\033[J");
}
void size(char *param) {
	string_trim(&param);

	if (!strcmp(param, "memoria")) {
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
		if (!strncmp(param, "PID ", 4)) {
			int pid;
			char *pid_c = string_substring_from(param, 4);
			string_trim(&pid_c);

			if (solo_numeros(pid_c)) {
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
					printf("Tamaño total del proceso (PID:%i) = %i Bytes, con %d página/s ocupada/s\n",
							pid, frames_usados * MARCO_SIZE, frames_usados);
				}
			}

			else {
				logear_error("Comando inválido", false);
			}

			free(pid_c);
		}

		else {
			logear_error("Comando inválido", false);
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
		CACHE_X_PROC = config_get_int_value(config, "CACHE_X_PROC");
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
char* remover_salto_linea(char *s) {
	int len = strlen(s);

	if (len > 0 && s[len - 1] == '\n')
		s[len - 1] = '\0';

	return s;
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

void hex_dump(FILE *output_file, int bytes, char *data) {
	int i_byte;
	for (i_byte = 0; i_byte < bytes; i_byte++) {
		DUMP_PRINT(output_file, "%02X", (unsigned char) data[i_byte]);
		if (i_byte % 2 == 1) {
			DUMP_PRINT(output_file, " ");
		}
		fflush(stdout);
	}

	for (i_byte = 0; i_byte < bytes; i_byte++) {
		if (iscntrl(data[i_byte])) {
			DUMP_PRINT(output_file, ".");
		}
		else {
			DUMP_PRINT(output_file, "%c", data[i_byte]);
		}
		fflush(stdout);
	}

	DUMP_PRINT(output_file, "\n");
}
