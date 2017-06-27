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
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>
#include "commons/collections/list.h"
#include "parser/parser.h"
#include "parser/metadata_program.h"
#include "qepd/qepd.h"

#define RUTA_CONFIG "config.cfg"
#define RUTA_LOG "cpu.log"

t_log* logger;
t_config* config;

/*
 * ↓ Parser Ansisop ↓
 */

// *************** Funciones
t_puntero definirVariable(t_nombre_variable identificador_variable);
t_puntero obtenerPosicionVariable(t_nombre_variable identificador_variable);
t_valor_variable dereferenciar(t_puntero direccion_variable);
void asignar(t_puntero direccion_variable, t_valor_variable valor);
t_valor_variable obtenerValorCompartida(t_nombre_compartida variable);
t_valor_variable asignarValorCompartida(t_nombre_compartida variable, t_valor_variable valor);
void irAlLabel(t_nombre_etiqueta etiqueta);
void llamarSinRetorno(t_nombre_etiqueta etiqueta);
void llamarConRetorno(t_nombre_etiqueta etiqueta, t_puntero donde_retornar);
void finalizar(void);
void retornar(t_valor_variable retorno);

// *************** Funciones nucleo
void wait(t_nombre_semaforo identificador_semaforo);
void parser_signal(t_nombre_semaforo identificador_semaforo);
t_puntero reservar(t_valor_variable espacio);
void liberar(t_puntero puntero);
t_descriptor_archivo abrir(t_direccion_archivo direccion, t_banderas flags);
void borrar(t_descriptor_archivo direccion);
void cerrar(t_descriptor_archivo descriptor_archivo);
void moverCursor(t_descriptor_archivo descriptor_archivo, t_valor_variable posicion);
void escribir(t_descriptor_archivo descriptor_archivo, void* informacion, t_valor_variable tamanio);
void leer(t_descriptor_archivo descriptor_archivo, t_puntero informacion, t_valor_variable tamanio);

AnSISOP_funciones funciones = {
		.AnSISOP_definirVariable			= definirVariable,
		.AnSISOP_obtenerPosicionVariable	= obtenerPosicionVariable,
		.AnSISOP_dereferenciar				= dereferenciar,
		.AnSISOP_asignar					= asignar,
		.AnSISOP_obtenerValorCompartida		= obtenerValorCompartida,
		.AnSISOP_asignarValorCompartida		= asignarValorCompartida,
		.AnSISOP_irAlLabel					= irAlLabel,
		.AnSISOP_llamarSinRetorno			= llamarSinRetorno,
		.AnSISOP_llamarConRetorno			= llamarConRetorno,
		.AnSISOP_finalizar					= finalizar,
		.AnSISOP_retornar					= retornar
};


AnSISOP_kernel funcionesnucleo = {
		.AnSISOP_wait	= wait,
		.AnSISOP_signal = parser_signal,
		.AnSISOP_reservar = reservar,
		.AnSISOP_liberar = liberar,
		.AnSISOP_abrir = abrir,
		.AnSISOP_cerrar = cerrar,
		.AnSISOP_borrar = borrar,
		.AnSISOP_moverCursor = moverCursor,
		.AnSISOP_escribir = escribir,
		.AnSISOP_leer = leer
};

/*
 * ↑ Parser Ansisop ↑
 */

/*
 * Colores:
 *
 * printf(RED "red\n" RESET);
 *
 * printf("This is " RED "red" RESET " and this is " BLU "blue" RESET "\n");
 *
 */
#define RED   "\x1B[31m"	// Señales, excepciones.
#define BLU   "\x1B[34m"	// Finalizacion de una rafaga del programa.
#define GRN   "\x1B[32m"	// Finalización correcta del programa (EXIT CODE == 1 o 0).
#define YEL   "\x1B[33m"	// Finalización incorrecta del programa (EXIT CODE < 0).
#define MAG   "\x1B[35m"	// Comienzo y finalización del proceso.
#define CYN   "\x1B[36m"	// Mensaje proveniente del Kernel.
#define WHT   "\x1B[37m"	// Acceso a memoria.
#define RESET "\x1B[0m"		// RESET

char IP_KERNEL[16]; // 255.255.255.255 = 15 caracteres + 1 ('\0')
int PUERTO_KERNEL;
char IP_MEMORIA[16];
int PUERTO_MEMORIA;

typedef struct Posicion_memoria {
	int numero_pagina;
	int offset;			// Desplazamiento dentro de la pagina
	int size;
} Posicion_memoria;

typedef struct Variable {
	char identificador;
	Posicion_memoria posicion;
} Variable;

typedef struct Entrada_stack {
	t_list *args;	// Con elementos de tipo Posicion_memoria
	t_list *vars;	// Con elementos de tipo Variable
	int retPos;
	Posicion_memoria retVar;
} Entrada_stack;

typedef struct PCB {
	int pid;
	int program_counter;
	int cantidad_paginas_codigo;

	t_size cantidad_instrucciones;
	t_intructions *instrucciones_serializado;

	t_size etiquetas_size;
	char *etiquetas;

	int	puntero_stack;
	t_list *indice_stack;

	t_list *tabla_archivos;

	int exit_code;
} PCB;

typedef struct posicionDeMemoriaAPedir {
	int processID;
	int numero_pagina;
	int offset;
	int size;
} posicionDeMemoriaAPedir;

typedef struct Variable_Global {
	t_nombre_compartida variable;
	t_valor_variable valor;
} Variable_Global;

typedef struct servidor { // Esto es más que nada una cheteada para poder usar al socket/identificador como constante en los switch
	int socket;
	char identificador;
} servidor;

servidor kernel;
servidor memoria;

bool programaVivitoYColeando; 	// Si el programa fallecio o no
bool elProgramaNoFinalizo; 		// Si el programa finalizo correctamente o sigue en curso
bool signalRecibida = false; 	// Si se recibe o no una señal SIGUSR1
bool tratarAtomicamente;
bool programaBloqueado;

char* actualInstruccion; // Lo modelo como variable global porque se me hace menos codigo analizar los header y demas.
void *buffer_solicitado;

PCB *actualPCB; // Programa corriendo
Posicion_memoria actualPosicion; // Actual posicion de memoria a utilizar
posicionDeMemoriaAPedir actualPosicionVariable; // Usado para comunicarse con el proceso memoria

int MARCO_SIZE;
int PAGINAS_SIZE;

enum Algoritmos {
	RR, FIFO
};

int algoritmoAUtilizar;

int quantum;
int quantum_sleep;

int analizarHeader(servidor servidor, headerDeLosRipeados header);
int cumplirDeseosDeKernel(char codigoDeOperacion, unsigned short bytesDePayload);
int cumplirDeseosDeMemoria(char codigoDeOperacion, unsigned short bytesDePayload);
int recibirAlgoDe(servidor servidor);
PCB *deserializar_PCB(void *buffer);
t_puntero calcularPuntero(Posicion_memoria posicion);
int agregarAlStack(t_nombre_variable identificador_variable);
void destruir_actualPCB(void);
void devolverPCB(int);
void ejecutarInstruccion();
void establecer_configuracion();
void leerMensaje(servidor servidor, unsigned short bytesDePayload);
void obtenerMarcoSize();
void obtenerPCB(unsigned short bytesDePayload);
void solicitarInstruccion();
void* serializar_PCB(PCB *pcb, int* buffersize);
bool esAtomico();
void ejecucion();
void finalizarPrograma(servidor servidor, int codigo);

/*
 * ↓ Señales ↓
 */

void rutinaSignal(int signal) {
	switch (signal) {
		case SIGUSR1:
	        printf(RED "[Señal] " RESET "Señal SIGUSR1 recibida\n");
	        signalRecibida = true;
	        break;
		default:
			printf(RED "[Señal] " RESET "Señal DESCONOCIDA recibida\n");
	}
}

/*
 * ↑ Señales ↑
 */

int main(void) {

	signal(SIGUSR1, rutinaSignal);

	configurar("cpu");

	memoria.identificador = MEMORIA;
	conectar(&memoria.socket, IP_MEMORIA, PUERTO_MEMORIA);
	handshake(memoria.socket, CPU);
	recv(memoria.socket, &MARCO_SIZE, 4, 0);
	logear_info("MARCO_SIZE: %d", MARCO_SIZE);

	kernel.identificador = KERNEL;
	conectar(&kernel.socket, IP_KERNEL, PUERTO_KERNEL);
	handshake(kernel.socket, CPU);

	printf(MAG "[CPU] " RESET "Comenzando el trabajo.\n");

	recibirAlgoDe(kernel);

	return 1;
}

void trabajar() {
	int i;

	programaVivitoYColeando = true;
	elProgramaNoFinalizo = true;

	char codigo;

	if(algoritmoAUtilizar == RR) {

		int i = 0;

		do {

			ejecucion();

			if(esAtomico()) {
				i--; // De esta forma aseguras que el programa corra hasta que deje de ser atomico.
			}

			i++;

		} while((i < quantum) || (!programaBloqueado && programaVivitoYColeando && elProgramaNoFinalizo));

	} else { // FIFO

		do {

			ejecucion();

		} while(!programaBloqueado && programaVivitoYColeando && elProgramaNoFinalizo);

	}

	if(programaBloqueado) {
		printf(YEL "[Programa] " RESET "El programa PID %i está bloqueado.\n", actualPCB->pid);
		codigo = PCB_BLOQUEADO;
	} else {

		if(programaVivitoYColeando) {

			if(elProgramaNoFinalizo) {
				printf(BLU "[Programa] " RESET "El programa PID %i finalizó su rafaga correctamente.\n", actualPCB->pid);
				codigo = PCB_INCOMPLETO;
			} else {
				printf(GRN "[Programa] " RESET "El programa PID %i finalizó correctamente.\n", actualPCB->pid);
				codigo = PCB_COMPLETO;
			}

		} else {
			printf(YEL "[Programa] " RESET RED "El programa PID %i finalizó incorrectamente.\n" RESET, actualPCB->pid);
			codigo = PCB_EXCEPCION;
		}

	}

	devolverPCB(codigo);
}

void ejecucion() {

	solicitarInstruccion();
	ejecutarInstruccion();
	actualPCB->program_counter++;

}

void solicitarInstruccion() {

	printf("Solicitando instrucción.\n");

	posicionDeMemoriaAPedir posicion;
	t_intructions instruccion = actualPCB->instrucciones_serializado[actualPCB->program_counter];

	posicion.processID = actualPCB->pid;
	posicion.numero_pagina = instruccion.start / MARCO_SIZE;
	posicion.offset = instruccion.start % MARCO_SIZE;

	int size;
	int bytes = instruccion.offset;
	int offset = 0;

	actualInstruccion = malloc(bytes);

	while (bytes > 0) {

		if(MARCO_SIZE - posicion.offset > bytes) {
			if(bytes < MARCO_SIZE) {
				posicion.size = bytes;
			} else {
				posicion.size = MARCO_SIZE;
			}
		} else {
			posicion.size = MARCO_SIZE - posicion.offset;
		}

		bytes -= posicion.size;

		enviar_header(memoria.socket, SOLICITAR_BYTES, sizeof(posicion));
		send(memoria.socket, &posicion, sizeof(posicion), 0);

		if(recibirAlgoDe(memoria) == -1) {
			printf(WHT "[Memoria] " RESET RED "Fallecio feo la obtencion de instruccion.\n" RESET);
		}

		memcpy(actualInstruccion + offset, buffer_solicitado, posicion.size);
		offset += posicion.size;

		posicion.numero_pagina++;
		posicion.offset = 0;
	}

	actualInstruccion[instruccion.offset - 1] = '\0';

	free(buffer_solicitado);

}

void ejecutarInstruccion() {

	if (algoritmoAUtilizar == RR)
		usleep(quantum_sleep * 1000);

	analizadorLinea(strdup(actualInstruccion), &funciones, &funcionesnucleo);
	free(actualInstruccion);
}

void devolverPCB(int operacion) {
	int buffersize;
	void *buffer = serializar_PCB(actualPCB, &buffersize);

	enviar_header(kernel.socket, operacion, buffersize);
	send(kernel.socket, buffer, buffersize, 0);

	logear_info("PCB devuelto");

	free(buffer);
	destruir_actualPCB();

	if(signalRecibida) {
		printf(MAG "[CPU] " RESET "Finalización del proceso.\n");
		exit(EXIT_SUCCESS);
	}
}

/*
 * ↑ Trabajar ↑
 */

/*
 * ↓ Llega información de alguno de los servidores ↓
 */

/**
 * Analiza el contenido del header, y respecto a ello realiza distintas acciones
 * devuelve -1 si el servidor causa problemas
 */

int recibirAlgoDe(servidor servidor) {
	headerDeLosRipeados header;
	int bytesRecibidos = recibir_header(servidor.socket, &header);
	if (bytesRecibidos > 0) {
		return analizarHeader(servidor, header);
	} else {
		logear_error("El servidor se desconectó. Finalizando...", false);
		exit(0); // En realidad se puede poner true en el logear_error y sacar esto pero me tira alto warning v:
	}

}

int analizarHeader(servidor servidor, headerDeLosRipeados header) {
	switch(servidor.identificador) {
		case KERNEL:
			return cumplirDeseosDeKernel(header.codigoDeOperacion, header.bytesDePayload);
			break;

		case MEMORIA:
			return cumplirDeseosDeMemoria(header.codigoDeOperacion, header.bytesDePayload);
			break;

		default:
			logear_error("Servidor desconocido, finalizando CPU...", true);
	}

	return 0; // Hubo un problema
}

int cumplirDeseosDeKernel(char codigoDeOperacion, unsigned short bytesDePayload) {
	switch(codigoDeOperacion) {
		case MENSAJE:
			leerMensaje(kernel, bytesDePayload);
			recibirAlgoDe(kernel);
			break;

		case QUANTUM:
			recv(kernel.socket, &quantum, sizeof(quantum), 0);
			printf("Quantum %i recibido.\n", quantum);
			recibirAlgoDe(kernel);
			break;

		case PCB_INCOMPLETO:
			printf("Obteniendo PCB.\n");
			obtenerPCB(bytesDePayload);
			//recv(kernel.socket, &tratarAtomicamente, sizeof(bool), 0); Lo comento porque tira error
			trabajar();
			recibirAlgoDe(kernel);
			break;

		case ALGORITMO_ACTUAL:
			recv(kernel.socket, &algoritmoAUtilizar, sizeof(int), 0);
			printf("El algoritmo actual es %i (RR = 0, FIFO = 1).\n", algoritmoAUtilizar);
			recibirAlgoDe(kernel);
			break;

		case QUANTUM_SLEEP:
			recv(kernel.socket, &quantum_sleep, sizeof(int), 0);
			printf("Quantum Sleep %i recibido.\n", quantum_sleep);
			recibirAlgoDe(kernel);
			break;

		case PAGINAS_STACK:
			recv(kernel.socket, &PAGINAS_SIZE, sizeof(int), 0);
			printf("Se recibe el tamaño de pagina, siendo %i bytes.\n", PAGINAS_SIZE);
			recibirAlgoDe(kernel);
			break;

		case OBTENER_VALOR_VARIABLE:
			buffer_solicitado = malloc(bytesDePayload);
			recv(kernel.socket, buffer_solicitado, sizeof(bytesDePayload), 0);
			break;

		case PETICION_CORRECTA:
			break;

		case BLOQUEAR:
			return 2;
			break;

		case SIGNAL:
			recv(kernel.socket, &tratarAtomicamente, sizeof(bool), 0);
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

		case EXCEPCION:
			;int codigo;
			recv(kernel.socket, &codigo, sizeof(codigo), 0);
			finalizarPrograma(kernel, codigo);
			return -1;
			break;

		default:
			printf(RED "CÓDIGO DE OPERACIÓN DESCONOCIDO POR PARTE DEL KERNEL: %i.\n" RESET, codigoDeOperacion);
			exit(EXIT_FAILURE);
	}

	return 1;
}

int cumplirDeseosDeMemoria(char codigoDeOperacion, unsigned short bytesDePayload) {

	switch(codigoDeOperacion) {
		case SOLICITAR_BYTES:
			buffer_solicitado = malloc(bytesDePayload);
			recv(memoria.socket, buffer_solicitado, bytesDePayload, 0);
			break;

		case PETICION_CORRECTA:
			break;

		case EXCEPCION:
			;int codigo;
			recv(memoria.socket, &codigo, sizeof(codigo), 0);
			finalizarPrograma(memoria, codigo);
			return -1;
			break;

		case FRAME_SIZE:
			obtenerMarcoSize();
			recibirAlgoDe(memoria);
			break;

		default:
			printf(RED "CÓDIGO DE OPERACIÓN DESCONOCIDO POR PARTE DE LA MEMORIA: %i.\n" RESET, codigoDeOperacion);
			exit(EXIT_FAILURE);
	}

	return 1;
}

/*
 * ↑ Llega información de alguno de los servidores ↑
 */

/*
 * ↓ Acatar ordenes de algunos de los servidores ↓
 */

void obtenerPCB(unsigned short bytesDePayload) {

	void *bufferPCB = malloc(bytesDePayload);
	int bytesRecibidos = recv(kernel.socket, bufferPCB, bytesDePayload, 0);

	if(bytesRecibidos <= 0) {
		logear_error("Kernel desconectado, finalizando CPU...", true);
	}

	actualPCB = deserializar_PCB(bufferPCB);

	logear_info("PCB obtenido");

	free(bufferPCB);
}

void finalizarPrograma(servidor servidor, int codigo) {
	switch(servidor.identificador) {
		case KERNEL:
			printf(RED "[Excepcion] " RESET "Debido a una excepcion de solicitud al Kernel, el proceso %i ripeo.\n", actualPCB->pid);
			break;

		case MEMORIA:
			printf(RED "[Excepcion] " RESET "Debido a una excepcion de solicitud a la Memoria, el proceso %i ripeo.\n", actualPCB->pid);
			break;
	}

	actualPCB->exit_code = codigo;

	programaVivitoYColeando = false;

}

void leerMensaje(servidor servidor, unsigned short bytesDePayload) {
	char* mensaje = malloc(bytesDePayload+1);
    recv(servidor.socket, mensaje, bytesDePayload, 0);
    mensaje[bytesDePayload]='\0';
    printf(CYN "Kernel: " RESET "%s\n", mensaje);
    free(mensaje);
}

/*
 * ↑ Acatar ordenes de algunos de los servidores ↑
 */

/*
 * ↓ Comunicarse con los servidores ↓
 */

int agregarAlStack(t_nombre_variable identificador_variable) {

	Entrada_stack *entrada = list_get(actualPCB->indice_stack, actualPCB->puntero_stack);

	Posicion_memoria *nuevaPosicion;
	nuevaPosicion = malloc(sizeof(Posicion_memoria));
	nuevaPosicion->numero_pagina = actualPCB->cantidad_paginas_codigo;
	nuevaPosicion->offset = stack_size() % PAGINAS_SIZE;
	nuevaPosicion->size = 4;

	if(identificador_variable >= '0' && identificador_variable <= '9') {

		list_add(entrada->args, nuevaPosicion);

	} else {

		Variable *nuevaVariable;
		nuevaVariable = malloc(sizeof(Variable));
		nuevaVariable->identificador = identificador_variable;
		nuevaVariable->posicion = *nuevaPosicion;
		list_add(entrada->vars, nuevaVariable);

	}

	return calcularPuntero(*nuevaPosicion);

}

void obtenerMarcoSize() {
	int bytesRecibidos = recv(kernel.socket, &MARCO_SIZE, sizeof(MARCO_SIZE), 0);

	if(bytesRecibidos <= 0) {
		printf(RED "EL KERNEL ENVÍO LOS FRAMES PARA EL ORTO.\n" RESET);
		exit(EXIT_FAILURE);
	}
}

/*
 * ↑ Comunicarse con los servidores ↑
 */

/*
 * ↓ Configuración del CPU y conexión a los servidores ↓
 */

void establecer_configuracion() {
	if(config_has_property(config, "PUERTO_KERNEL")) {
		PUERTO_KERNEL = config_get_int_value(config, "PUERTO_KERNEL");
		logear_info("Puerto Kernel: %i \n",PUERTO_KERNEL);
	} else {
		logear_error("Error al leer el puerto del Kernel", true);
	}
	if(config_has_property(config, "IP_KERNEL")) {
		strcpy(IP_KERNEL,config_get_string_value(config, "IP_KERNEL"));
		logear_info("IP Kernel: %s \n", IP_KERNEL);
	} else {
		logear_error("Error al leer la IP del Kernel", true);
	}
	if(config_has_property(config, "PUERTO_MEMORIA")) {
		PUERTO_MEMORIA = config_get_int_value(config, "PUERTO_MEMORIA");
		logear_info("Puerto Memoria: %i \n", PUERTO_MEMORIA);
	} else {
		logear_error("Error al leer el puerto de la Memoria", true);
	}
	if(config_has_property(config, "IP_MEMORIA")){
		strcpy(IP_MEMORIA,config_get_string_value(config, "IP_MEMORIA"));
		logear_info("IP Memoria: %s \n", IP_MEMORIA);
	} else {
		logear_error("Error al leer la IP de la Memoria", true);
	}

}

/*
 * ↑ Configuración del CPU y conexión a los servidores ↑
 */

/*
 * ↓ Funciones auxiliares del CPU ↓
 */

int stack_size() {

	int total = 0;

	void entrada_size(void* a) {
		Entrada_stack *entrada = a;
		total += 4 * (entrada->args->elements_count + entrada->vars->elements_count);
	}

	list_iterate(actualPCB->indice_stack, &entrada_size);

	return total;

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

bool hay_stack_overflow_si_agrego_otra_variable() {

	Entrada_stack *miEntradaStack = list_get(actualPCB->indice_stack, actualPCB->puntero_stack);

	Entrada_stack miEntradaDefinitiva = *miEntradaStack;

	return stack_size() + 4 >= PAGINAS_SIZE * MARCO_SIZE;

}

bool ya_existe_variable(t_nombre_variable identificador_variable) {

	_Bool tienenMismoIdentificador(void* variable) {
		Variable *miVariable = variable;
		return miVariable->identificador == identificador_variable;
	}

	Entrada_stack *entrada_actual = list_get(actualPCB->indice_stack, actualPCB->puntero_stack);

	return list_any_satisfy(entrada_actual->vars, &tienenMismoIdentificador);

}

t_puntero calcularPuntero(Posicion_memoria posicion) {
	return posicion.numero_pagina * MARCO_SIZE + posicion.offset;
}

bool esAtomico() {
	return tratarAtomicamente;
}

void *list_serialize(t_list* list, int element_size, int *buffersize) {
	int element_count = list_size(list),offset = 8;
	*buffersize = offset + element_count*element_size;
	void *buffer = malloc(*buffersize);
	memcpy(buffer,&element_count,4);
	memcpy(buffer+4,&element_size,4);
	void copy(void *param) {
		memcpy(buffer+offset,param,element_size);
		offset += element_size;
	}
	list_iterate(list,&copy);
	return buffer;
}

t_list *list_deserialize(void *buffer) {
	t_list *new_list = list_create();
	int offset = 8,i,element_count,element_size;
	memcpy(&element_count,buffer,4);
	memcpy(&element_size,buffer+4,4);
	for (i=0;i<element_count;i++) {
		void *element = malloc(element_size);
		memcpy(element,buffer+offset,element_size);
		list_add(new_list,element);
		offset += element_size;
	}
	return new_list;
}

void *serializar_PCB(PCB *pcb, int* buffersize) {
	int instrucciones_size = pcb->cantidad_instrucciones * sizeof(t_intructions);
	int stack_size;
	void *stack_buffer = list_serialize(pcb->indice_stack,sizeof(Entrada_stack), &stack_size);
	void copy(void *param) {
		Entrada_stack *entrada = (Entrada_stack*) param;
		int args_size;
		int vars_size;
		void *args_buffer = list_serialize(entrada->args,sizeof(Posicion_memoria), &args_size);
		void *vars_buffer = list_serialize(entrada->vars,sizeof(Variable), &vars_size);
		stack_buffer = realloc(stack_buffer, stack_size + args_size + vars_size);
		memcpy(stack_buffer + stack_size, args_buffer, args_size);
		memcpy(stack_buffer + stack_size + args_size, vars_buffer, vars_size);
		stack_size += args_size + vars_size;
		free(args_buffer);
		free(vars_buffer);
	}
	list_iterate(pcb->indice_stack,&copy);
	*buffersize = sizeof(PCB) + instrucciones_size + pcb->etiquetas_size + stack_size;
	void *buffer = malloc(*buffersize);
	int offset = 0;
	memcpy(buffer+offset, pcb, sizeof(PCB));
	offset += sizeof(PCB);
	memcpy(buffer + offset, pcb->instrucciones_serializado, instrucciones_size);
	offset += instrucciones_size;
	memcpy(buffer + offset, pcb->etiquetas, pcb->etiquetas_size);
	offset += pcb->etiquetas_size;
	memcpy(buffer + offset, stack_buffer, stack_size);
	free(stack_buffer);

	return buffer;
}

PCB *deserializar_PCB(void *buffer) {
	PCB *pcb = malloc(sizeof(PCB));
	int offset = 0;
	memcpy(pcb, buffer, sizeof(PCB));
	offset += sizeof(PCB);
	int instrucciones_size = pcb->cantidad_instrucciones * sizeof(t_intructions);
	pcb->instrucciones_serializado = malloc(instrucciones_size);
	memcpy(pcb->instrucciones_serializado, buffer + offset, instrucciones_size);
	offset += instrucciones_size;
	pcb->etiquetas = malloc(pcb->etiquetas_size);
	memcpy(pcb->etiquetas, buffer + offset, pcb->etiquetas_size);
	offset += pcb->etiquetas_size;
	pcb->indice_stack = list_deserialize(buffer + offset);
	offset += list_size(pcb->indice_stack) * sizeof(Entrada_stack) + 8;
	void copy(void *param) {
		Entrada_stack *entrada = (Entrada_stack*) param;
		entrada->args = list_deserialize(buffer + offset);
		int args_size = list_size(entrada->args) * sizeof(Posicion_memoria) + 8;
		offset += args_size;
		entrada->vars = list_deserialize(buffer + offset);
		int vars_size = list_size(entrada->vars) * sizeof(Variable) + 8;
		offset += vars_size;
	}
	list_iterate(pcb->indice_stack,&copy);
	return pcb;
}

void destruir_entrada_stack(void *param) {
	Entrada_stack *entrada = (Entrada_stack *) param;
	list_destroy_and_destroy_elements(entrada->vars, free);
	list_destroy_and_destroy_elements(entrada->args, free);
	free(entrada);
}

void destruir_actualPCB(void) {
	free(actualPCB->etiquetas);
	free(actualPCB->instrucciones_serializado);
	list_destroy_and_destroy_elements(actualPCB->indice_stack, destruir_entrada_stack);
	free(actualPCB);
}

/*
 * ↑ Funciones auxiliares del CPU ↑
 */

/*
 * ↓ Funciones auxiliares del Parser ↓
 */

/*
 * ↑ Funciones auxiliares del Parser ↑
 */

/*
 * ↓ Parsear tranqui ↓
 */

t_puntero definirVariable(t_nombre_variable identificador_variable) {
	printf("definirVariable: %c.\n", identificador_variable);

	if(ya_existe_variable(identificador_variable)) {

		printf(RED "SE REDEFINIO LA VARIABLE %c.\n" RESET, identificador_variable);
		actualPCB->exit_code = REDECLARACION_VARIABLE;

	} else {

		if(hay_stack_overflow_si_agrego_otra_variable()) {

			printf(RED "STACK OVERFLOW.\n" RESET);
			actualPCB->exit_code = SOBRECARGA_STACK;

		} else {

			return agregarAlStack(identificador_variable);

		}
	}

	return 0;
}

t_puntero obtenerPosicionVariable(t_nombre_variable identificador_variable) {
	printf("Obtener posicion de la variable: %c.\n", identificador_variable);

	Entrada_stack *entrada = list_get(actualPCB->indice_stack, actualPCB->puntero_stack);

	_Bool mismo_identificador(void* param) {
		Variable* var = (Variable*) param;
		return identificador_variable == var->identificador;
	}

	Variable *miVariable = list_find(entrada->vars, &mismo_identificador);

	t_puntero miPuntero;

	if(miVariable == NULL) {

		Posicion_memoria *posicion = list_get(entrada->args, atoi(&identificador_variable));

		if(posicion == NULL) {

			printf(RED "No se pudo obtener la posición de la variable.\n" RESET);
			programaVivitoYColeando = false;
			actualPCB->exit_code = VARIABLE_SIN_DECLARAR;
			return 0;

		} else {

			miPuntero = calcularPuntero(*posicion);

		}

	} else {

		miPuntero = calcularPuntero(miVariable -> posicion);

	}

	printf("La posición de la variable es %i.\n", miPuntero);
	return miPuntero;
}

t_valor_variable dereferenciar(t_puntero direccion_variable) {
	printf("Dereferenciar: %i\n", direccion_variable);

	posicionDeMemoriaAPedir posicion;
	posicion.processID = actualPCB->pid;
	posicion.numero_pagina = direccion_variable / MARCO_SIZE;
	posicion.offset = direccion_variable % MARCO_SIZE;
	posicion.size = 4;

	enviar_header(memoria.socket, SOLICITAR_BYTES, sizeof(posicion));
	send(memoria.socket, &posicion, sizeof(posicion), 0);

	if (recibirAlgoDe(memoria) > 0) {
		t_valor_variable *miValor = buffer_solicitado;
		t_valor_variable miNuevoValor = *miValor;

		free(buffer_solicitado);
		printf(WHT "[Memoria] " RESET "Devolvió exitosamente un valor de la memoria.\n");
		printf("Valor dereferenciado: %i.\n", miNuevoValor);
		return miNuevoValor;
	}

	printf(WHT "[Memoria] " RESET RED "Acceso invalido a la memoria.\n" RESET);
	return 0;

}

void asignar(t_puntero direccion_variable, t_valor_variable valor) {
	printf("Se asigno una variable con el valor %i en la direccion %i.\n", valor, direccion_variable);

	posicionDeMemoriaAPedir posicion;
	posicion.processID = actualPCB->pid;
	posicion.numero_pagina = direccion_variable / MARCO_SIZE;
	posicion.offset = direccion_variable % MARCO_SIZE;
	posicion.size = 4;

	enviar_header(memoria.socket, ALMACENAR_BYTES, sizeof(posicion));
	send(memoria.socket, &posicion, sizeof(posicion), 0);
	send(memoria.socket, &valor, sizeof(valor), 0);

	if (recibirAlgoDe(memoria) > 0) {
		printf(WHT "[Memoria] " RESET "Se pudo asignar correctamente una variable.\n" RESET);
	} else {
		printf(WHT "[Memoria] " RESET RED "Fallecio feo la asignación.\n" RESET);
	}

}

t_valor_variable obtenerValorCompartida(t_nombre_compartida variable) {
	printf("Se obtiene la variable compartida %s\n", variable);

	enviar_header(kernel.socket, OBTENER_VALOR_VARIABLE, strlen(variable) + 1);
	send(kernel.socket, variable, strlen(variable) + 1, 0);

	if(recibirAlgoDe(kernel) > 0) {
		t_valor_variable *miValor = buffer_solicitado;
		free(buffer_solicitado);
		printf("El valor de la variable compartida es %i.\n", *miValor);
		return *miValor;
	}

	printf(CYN "[Kernel] " RESET RED "Fallecio feo la obtención de valor compartida.\n" RESET);

	return 0;

}

t_valor_variable asignarValorCompartida(t_nombre_compartida variable, t_valor_variable valor) {
	printf("Se asigna la variable compartida %s con el valor %d.\n", variable, valor);

	enviar_header(kernel.socket, ASIGNAR_VALOR_VARIABLE, strlen(variable) + 1 + sizeof(valor));
	send(kernel.socket, &variable, sizeof(variable), 0);
	send(kernel.socket, &valor, sizeof(valor), 0);

	if(recibirAlgoDe(kernel) > 0) {
		printf("Se asigno la variable exitosamente.\n");
		return valor;
	}

	printf(CYN "[Kernel] " RESET RED "Fallecio feo la asignación de valor compartida.\n" RESET);

	return valor;
}

void irAlLabel(t_nombre_etiqueta etiqueta) {

	printf("Se va al label %s.\n", etiqueta);

	int nuevo_program_counter = metadata_buscar_etiqueta(etiqueta, actualPCB->etiquetas, actualPCB->etiquetas_size);

	if (nuevo_program_counter < 0) {

		programaVivitoYColeando = false;
		actualPCB->exit_code = ETIQUETA_INEXISTENTE;
		printf(RED "[Excepcion]" RESET "El label %s no existe.\n", etiqueta);

	} else {

		printf("Se pudo ir al label correctamente.\n");
		actualPCB->program_counter = nuevo_program_counter - 1;
		//Ese -1 místico es porque luego voy a incrementar el program counter
		//en 1, entonces se anula.. osea... -1 + 1 = 0, understand? meh

	}

}

void llamarSinRetorno(t_nombre_etiqueta etiqueta) {
	printf("Se llama sin retorno hacia %s.\n", etiqueta);

	int viejoPC = actualPCB->program_counter;

	irAlLabel(etiqueta);

	if(programaVivitoYColeando) {
		Entrada_stack *nueva_entrada = malloc(sizeof(Entrada_stack));

		nueva_entrada->args = list_create();
		nueva_entrada->vars = list_create();

		nueva_entrada->retPos = viejoPC + 1;
		//Hay que volver a la instrucción siguiente de la que partí

		list_add(actualPCB->indice_stack, nueva_entrada);
		actualPCB->puntero_stack++;
	}

}

void llamarConRetorno(t_nombre_etiqueta etiqueta, t_puntero donde_retornar) {
	printf("Se llama con retorno hacia %s.\n", etiqueta);

	int viejoPC = actualPCB->program_counter;

	irAlLabel(etiqueta);

	if(programaVivitoYColeando) {

		Entrada_stack *nuevaEntrada = malloc(sizeof(Entrada_stack));

		nuevaEntrada->args = list_create();
		nuevaEntrada->vars = list_create();
		nuevaEntrada->retPos = viejoPC + 1;
		nuevaEntrada->retVar.numero_pagina = donde_retornar / MARCO_SIZE;
		nuevaEntrada->retVar.offset = donde_retornar % MARCO_SIZE;
		nuevaEntrada->retVar.size = 4;

		list_add(actualPCB->indice_stack, nuevaEntrada);
		actualPCB->puntero_stack++;

	}

}

void finalizar(void) {
	if (actualPCB->puntero_stack > 0) {

		printf("Llamado a función finalizado correctamente.\n");

		Entrada_stack *entrada = list_remove(actualPCB->indice_stack, actualPCB->puntero_stack);

		actualPCB->puntero_stack--;
		actualPCB->program_counter = entrada->retPos - 1;

		destruir_entrada_stack(entrada);

	} else {

		printf("Programa finalizado correctamente.\n");
		actualPCB->exit_code = 0;
		elProgramaNoFinalizo = false;

	}

}

void retornar(t_valor_variable retorno) {
	printf("Se retorna el valor %i.\n", retorno);

	Entrada_stack *entrada = list_remove(actualPCB->indice_stack, actualPCB->puntero_stack);
	asignar(calcularPuntero(entrada->retVar), retorno);
	destruir_entrada_stack(entrada);

}

/*
 * ↑ Parsear tranqui ↑
 */

/*
 * ↓ Parsear Kernel ↓
 */

void wait(t_nombre_semaforo identificador_semaforo) {
	printf("El semaforo %s utiliza wait.\n", identificador_semaforo);

	enviar_header(kernel.socket, WAIT, sizeof(identificador_semaforo) + 1);
	send(kernel.socket, identificador_semaforo, sizeof(identificador_semaforo) +1, 0);

	int devolucionDeKernel = recibirAlgoDe(kernel);

	if(devolucionDeKernel == 1) { // No esta bloqueado
		printf("El semaforo %s utilizo wait sin quedar bloqueado.\n", identificador_semaforo);
		tratarAtomicamente = true;
	}

	if(devolucionDeKernel == 2) { // Esta bloqueado
		printf("El semaforo %s utilizo wait y quedo bloqueado.\n", identificador_semaforo);
		programaBloqueado = true;
	}

	if(devolucionDeKernel == -1) {
		printf(CYN "[Kernel] " RESET RED "Fallo en la utilización de wait.\n" RESET);
	}

}

void parser_signal(t_nombre_semaforo identificador_semaforo) { // No contemplo que en un proceso un semaforo haga SIGNAL sin haber hecho WAIT
	printf("El semaforo %s utiliza signal.\n", identificador_semaforo);

	enviar_header(kernel.socket, SIGNAL, sizeof(identificador_semaforo) + 1);
	send(kernel.socket, identificador_semaforo, sizeof(identificador_semaforo) + 1, 0);

	if(recibirAlgoDe(kernel) > 0) {

		printf("El semaforo %s utilizo signal exitosamente.\n", identificador_semaforo);

	} else {

		printf(CYN "[Kernel] " RESET RED "Fallo en la utilizacion de signal.\n" RESET);

	}
}

t_puntero reservar(t_valor_variable espacio) {
	printf("Se pretende reservar %i bytes.\n", espacio);

	enviar_header(kernel.socket, SOLICITAR_BYTES, espacio);
	if(recibirAlgoDe(kernel) == -1) {

		printf(CYN "[Kernel] " RESET RED "RIP reserva de memoria.\n" RESET);
		return 0;

	}

	printf(CYN "[Kernel] " RESET "Se reservo %i bytes de memoria exitosamente.\n", espacio);

	Posicion_memoria *miPosicion = buffer_solicitado;

	free(buffer_solicitado);

	// Habria que guardar la posicion de la reserva de memoria ¿?

	return calcularPuntero(*miPosicion);

}

void liberar(t_puntero puntero) {
	printf("Se pretende liberar memoria de la posicion %i.\n", puntero);

	Posicion_memoria miPosicion;

	miPosicion.numero_pagina = puntero / MARCO_SIZE;
	miPosicion.offset = puntero % MARCO_SIZE;
	miPosicion.size = puntero - miPosicion.offset;

	enviar_header(kernel.socket, LIBERAR_MEMORIA, sizeof(miPosicion));
	send(kernel.socket, &miPosicion, sizeof(miPosicion), 0);

	if(!recibirAlgoDe(kernel)) {
		printf(CYN "[Kernel] " RESET RED "Ripeo la liberación de memoria.\n" RESET);
	}

	printf("Se libero memoria exitosamente.\n");
}

t_descriptor_archivo abrir(t_direccion_archivo direccion, t_banderas flags) {

	printf("Se abre un archivo en la direccion %s.\n", direccion);

	enviar_header(kernel.socket, ABRIR_ARCHIVO, strlen(direccion));
	send(kernel.socket, direccion, strlen(direccion), 0);
	send(kernel.socket, &flags, sizeof(flags), 0);

	t_descriptor_archivo miDescriptor = recibirAlgoDe(kernel);

	if(miDescriptor == -1) {
		printf(CYN "[Kernel] " RESET RED "Fallecio la apertura de archivo.\n" RESET);
		return 0;
	}

	printf(CYN "[Kernel] " RESET "Se abrio el archivo correctamente.\n");

	return miDescriptor;

}

void borrar(t_descriptor_archivo direccion) {

	printf("Se quiere borrar archivo con descriptor %i.\n", direccion);

	enviar_header(kernel.socket, BORRAR_ARCHIVO, sizeof(direccion));
	send(kernel.socket, &direccion, sizeof(direccion), 0);

	if(recibirAlgoDe(kernel) > 0) {
		printf(CYN "[Kernel] " RESET "Se borro el archivo exitosamente.\n" RESET);
	} else {
		printf(CYN "[Kernel] " RESET RED "Fallecio la borración de archivo.\n" RESET);
	}

}

void cerrar(t_descriptor_archivo descriptor_archivo) {

	printf("Se quiere cerrar archivo con descriptor %i", descriptor_archivo);

	enviar_header(kernel.socket, CERRAR_ARCHIVO, sizeof(descriptor_archivo));
	send(kernel.socket, &descriptor_archivo, sizeof(descriptor_archivo), 0);

	if(recibirAlgoDe(kernel) > 0) {
		printf(CYN "[Kernel] " RESET "Se cerró el archivo exitosamente.\n" RESET);
	} else {
		printf(CYN "[Kernel] " RESET RED "Fallecio la cerración de archivo.\n" RESET);
	}

}

void moverCursor(t_descriptor_archivo descriptor_archivo, t_valor_variable posicion) {

	printf("Se quiere mover el cursor en el descriptor %i a la posicion %i.\n", descriptor_archivo, posicion);

	enviar_header(kernel.socket, MOVER_CURSOR, sizeof(descriptor_archivo) + sizeof(posicion));
	send(kernel.socket, &descriptor_archivo, sizeof(descriptor_archivo), 0);
	send(kernel.socket, &posicion, sizeof(posicion), 0);

	if(recibirAlgoDe(kernel) > 0) {
		printf(CYN "[Kernel] " RESET "Se movió el cursor exitosamente.\n" RESET);
	} else {
		printf(CYN "[Kernel] " RESET RED "Fallecio el movimiento del cursor.\n" RESET);
	}

}

void escribir(t_descriptor_archivo descriptor_archivo, void* informacion, t_valor_variable tamanio) {

	printf("Se quiere escribir %i bytes en un archivo con descriptor %i.\n", tamanio, descriptor_archivo);

	enviar_header(kernel.socket, ESCRIBIR_ARCHIVO, tamanio);
	send(kernel.socket, &descriptor_archivo, sizeof(descriptor_archivo), 0);
	send(kernel.socket, informacion, tamanio, 0);

	if(recibirAlgoDe(kernel) > 0) {
		printf(CYN "[Kernel] " RESET "Se escribió el archivo exitosamente.\n" RESET);
	} else {
		printf(CYN "[Kernel] " RESET RED "Fallecio la escritura de archivo.\n" RESET);
	}

}

void leer(t_descriptor_archivo descriptor_archivo, t_puntero informacion, t_valor_variable tamanio) {

	printf("Se quiere leer %i bytes de un archivo con descriptor %i", tamanio, descriptor_archivo);

	Posicion_memoria miPosicion;
	miPosicion.numero_pagina = informacion / MARCO_SIZE;
	miPosicion.offset = informacion % MARCO_SIZE;
	miPosicion.size = tamanio;

	enviar_header(kernel.socket, LEER_ARCHIVO, tamanio);
	send(kernel.socket, &miPosicion, sizeof(miPosicion), 0);
	send(kernel.socket, &descriptor_archivo, sizeof(descriptor_archivo), 0);

	if (recibirAlgoDe(kernel) > 0) {
		printf(CYN "[Kernel] " RESET "Lectura correcta del archivo.\n");
	} else {
		printf(CYN "[Kernel] " RESET RED "Murio la lectura.\n" RESET);
	}

}

/*
 * ↑ Parsear Kernel ↑
 */
