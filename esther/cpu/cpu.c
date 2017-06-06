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

#define VIVITO_Y_COLEANDO 1; // No se pueden revisar los exit code previo a que ripee

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

char* actualInstruccion; // Lo modelo como variable global porque se me hace menos codigo analizar los header y demas.
void *buffer_solicitado;

PCB *actualPCB; // Programa corriendo
Posicion_memoria actualPosicion; // Actual posicion de memoria a utilizar
posicionDeMemoriaAPedir actualPosicionVariable; // Usado para comunicarse con el proceso memoria

int MARCO_SIZE;

char quantum;

int analizarHeader(servidor servidor, headerDeLosRipeados header);
int cumplirDeseosDeKernel(char codigoDeOperacion, unsigned short bytesDePayload);
int cumplirDeseosDeMemoria(char codigoDeOperacion, unsigned short bytesDePayload);
int pedirMemoria();
int recibirAlgoDe(servidor servidor);
PCB *deserializar_PCB(void *buffer);
t_puntero calcularPuntero(Posicion_memoria posicion);
void agregarAlStack(t_nombre_variable identificador_variable, Posicion_memoria posicionDeMemoria);
void destruir_actualPCB(void);
void devolverPCB(int);
void ejecutarInstruccion();
void establecer_configuracion();
void finalizarPrograma();
void leerMensaje(servidor servidor, unsigned short bytesDePayload);
void obtenerInstruccionDeMemoria();
void obtenerMarcoSize();
void obtenerPCB(unsigned short bytesDePayload);
void obtenerPosicionDeMemoria();
void solicitarInstruccion();
void* serializar_PCB(PCB *pcb, int* buffersize);

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

	for(i = 0; i < quantum; i++) {
		solicitarInstruccion();
		ejecutarInstruccion();
		actualPCB->program_counter++;

		if(programaVivitoYColeando) {
			if(elProgramaNoFinalizo) {
				printf(BLU "[Programa] " RESET "El programa PID %i finalizó su rafaga correctamente.\n", actualPCB->pid);
				codigo = PCB_INCOMPLETO;

			} else {
				printf(GRN "[Programa] " RESET "El programa PID %i finalizó correctamente.\n", actualPCB->pid);
				codigo = PCB_COMPLETO;
				break;
			}

		} else {
			printf(YEL "[Programa] " RESET RED "El programa PID %i finalizó incorrectamente.\n" RESET, actualPCB->pid);
			codigo = PCB_EXCEPCION;
			break;
		}
	};

	devolverPCB(codigo);
}

void solicitarInstruccion() {
	logear_info("Solicitando instruccion...");

	posicionDeMemoriaAPedir posicion;
	t_intructions instruction = actualPCB->instrucciones_serializado[actualPCB->program_counter];

	posicion.processID = actualPCB->pid;
	posicion.numero_pagina = 0;
	posicion.size = instruction.offset;
	posicion.offset = instruction.start;

	enviar_header(memoria.socket, SOLICITAR_BYTES, sizeof(posicion));
	send(memoria.socket, &posicion, sizeof(posicion), 0);

	// Ahora esperamos la instruccion

	recibirAlgoDe(memoria);

	// La guardamos en actualInstruccion para que luego se ejecute

	actualInstruccion = buffer_solicitado;
}

void ejecutarInstruccion() {
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
			obtenerPCB(bytesDePayload);
			trabajar();
			recibirAlgoDe(kernel);
			break;

		case PEDIR_MEMORIA_VARIABLE_OK:
			obtenerPosicionDeMemoria();
			break;

		case OBTENER_VALOR_VARIABLE_OK:
			;t_valor_variable miValor;
			recv(kernel.socket, &miValor, sizeof(miValor), 0);
			return miValor;
			break;

		case ASIGNAR_VALOR_VARIABLE_OK:
			break;

		case RETORNAR_VARIABLE_OK:
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

		case EXCEPCION_DE_SOLICITUD:
			finalizarPrograma(kernel);
			return 0;
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

		case EXCEPCION_DE_SOLICITUD:
			finalizarPrograma(memoria);
			return 0;
			break;

		case FRAME_SIZE:
			obtenerMarcoSize();
			break;

		case INSTRUCCION_OK:
			obtenerInstruccionDeMemoria();
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

void finalizarPrograma(servidor servidor) {
	switch(servidor.identificador) {
		case KERNEL:
			printf(RED "[Excepcion] " RESET "Debido a una excepcion de solicitud al Kernel, el proceso %i ripeo.\n", actualPCB->pid);
			actualPCB->exit_code = EXCEPCION_KERNEL;
			break;

		case MEMORIA:
			printf(RED "[Excepcion] " RESET "Debido a una excepcion de solicitud a la Memoria, el proceso %i ripeo.\n", actualPCB->pid);
			actualPCB->exit_code = EXCEPCION_MEMORIA;
			break;
	}

	programaVivitoYColeando = false;

}

void leerMensaje(servidor servidor, unsigned short bytesDePayload) {
	char* mensaje = malloc(bytesDePayload+1);
    recv(servidor.socket, mensaje, bytesDePayload, 0);
    mensaje[bytesDePayload]='\0';
    printf(CYN "Kernel: " RESET "%s\n", mensaje);
    free(mensaje);
}

void obtenerInstruccionDeMemoria() {
	unsigned short instruccionSize;
	instruccionSize = actualPCB->instrucciones_serializado->offset - actualPCB->instrucciones_serializado->start + 1; // "+ 1" porque hay un byte que en la resta se lo come

	actualInstruccion = malloc(instruccionSize + 1); // +1 por el '\0'

	unsigned short bytesRecibidos = recv(memoria.socket, actualInstruccion, instruccionSize, 0); // Recibo la instruccion

	if(bytesRecibidos <= 0) {
		printf(RED "LA MEMORIA ENVÍO UNA INSTRUCCION PARA EL ORTO.\n" RESET);
		exit(EXIT_FAILURE);
	}

	actualInstruccion[instruccionSize] = '\0';

}

/*
 * ↑ Acatar ordenes de algunos de los servidores ↑
 */

/*
 * ↓ Comunicarse con los servidores ↓
 */

int pedirMemoria() {

	headerDeLosRipeados header;
	header.codigoDeOperacion = PEDIR_MEMORIA_VARIABLE;
	header.bytesDePayload = actualPCB->pid; // Acá pongo el ID del proceso que pide memoria

	send(kernel.socket, &header, sizeof(header), 0);

	printf(CYN "[Kernel] " RESET "Pidiendo memoria para variable.\n");

	// Ahora esperemos el ok

	return recibirAlgoDe(kernel);

}

void obtenerPosicionDeMemoria() {

	int bytesRecibidos = recv(kernel.socket, &actualPosicionVariable, sizeof(posicionDeMemoriaAPedir), 0);

	if(bytesRecibidos <= 0) {
		printf(RED "EL KERNEL ENVÍO UNA POSICION DE MEMORIA PARA EL ORTO.\n" RESET);
		exit(EXIT_FAILURE);
	}

	actualPosicion.numero_pagina = actualPosicionVariable.numero_pagina;
	actualPosicion.offset = actualPosicionVariable.offset;
	actualPosicion.size = actualPosicionVariable.size;

}

void agregarAlStack(t_nombre_variable identificador_variable, Posicion_memoria posicionDeMemoria) {
	Variable *nuevaVariable;

	nuevaVariable = malloc(sizeof(Variable));

	nuevaVariable->identificador = identificador_variable;
	nuevaVariable->posicion.numero_pagina = posicionDeMemoria.numero_pagina;
	nuevaVariable->posicion.offset = posicionDeMemoria.offset;
	nuevaVariable->posicion.size = posicionDeMemoria.size;

	Entrada_stack *entrada = list_get(actualPCB->indice_stack, actualPCB->puntero_stack);

	list_add(entrada->vars, nuevaVariable);

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

t_puntero calcularPuntero(Posicion_memoria posicion) {
	return posicion.numero_pagina * MARCO_SIZE + posicion.offset;
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
	printf("definirVariable: %c\n", identificador_variable);

	if(pedirMemoria() == 1) {
		printf(WHT "[Memoria] " RESET "Devolvió exitosamente una posición de memoria.\n");
		agregarAlStack(identificador_variable, actualPosicion);
		return calcularPuntero(actualPosicion);
	} else {
		printf(WHT "[Memoria] " RESET RED "Acceso invalido a la memoria.\n" RESET);
		return 0;
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

	if(miVariable == NULL) {
		printf(RED "No se pudo obtener la posición de la variable" RESET);
		programaVivitoYColeando = false;
		actualPCB->exit_code = EXCEPCION_MEMORIA;
		return 0;
	}

	t_puntero miPuntero = calcularPuntero(miVariable -> posicion);

	printf("La posición de la variable es %i\n", miPuntero);
	return miPuntero;
}

t_valor_variable dereferenciar(t_puntero direccion_variable) {
	printf("Dereferenciar: %i\n", direccion_variable);

	t_valor_variable miValor;
	posicionDeMemoriaAPedir posicion;

	Entrada_stack *entrada = list_get(actualPCB->indice_stack, actualPCB->puntero_stack);

	_Bool compararDireccionVariable(void* param) {
		Variable* var = (Variable*) param;
		return direccion_variable == calcularPuntero(var->posicion);
	}

	Variable *miVariable = list_find(entrada->vars, &compararDireccionVariable);

	posicion.processID = actualPCB->pid;
	posicion.numero_pagina = miVariable->posicion.numero_pagina;
	posicion.offset = miVariable->posicion.offset;
	posicion.size = miVariable->posicion.size;

	headerDeLosRipeados header;
	header.codigoDeOperacion = SOLICITAR_BYTES;
	header.bytesDePayload = sizeof(posicion);

	send(memoria.socket, &header, sizeof(header), 0);
	send(memoria.socket, &posicion, sizeof(posicion), 0);

	if (recibirAlgoDe(memoria)) {
		miValor = (t_valor_variable) buffer_solicitado;
		printf(WHT "[Memoria] " RESET "Devolvió exitosamente un valor de la memoria.\n");
		printf("Valor dereferenciado: %i", miValor);
		return miValor;
	} else {
		printf(WHT "[Memoria] " RESET RED "Acceso invalido a la memoria.\n" RESET);
		return 0; // Esto devuelve 0 porque algo tiene que devolver. Aun así, el programa deja de correr y se le envia el PCB al Kernel
	}

}

void asignar(t_puntero direccion_variable, t_valor_variable valor) {
	printf("Se asigno una variable con el valor %i en la direccion %i.\n", valor, direccion_variable);

	posicionDeMemoriaAPedir posicion;

	Entrada_stack *entrada = list_get(actualPCB->indice_stack, actualPCB->puntero_stack);

	_Bool compararDireccionVariable(void* param) {
		Variable* var = (Variable*) param;
		return direccion_variable == calcularPuntero(var->posicion);
	}

	Variable *miVariable = list_find(entrada->vars, &compararDireccionVariable);

	posicion.processID = actualPCB->pid;
	posicion.numero_pagina = miVariable->posicion.numero_pagina;
	posicion.offset = miVariable->posicion.offset;
	posicion.size = miVariable->posicion.size;

	enviar_header(memoria.socket, ASIGNAR_VALOR_VARIABLE, sizeof(posicion));
	send(memoria.socket, &posicion, sizeof(posicion), 0);
	send(memoria.socket, &valor, sizeof(valor), 0);

	if (recibirAlgoDe(memoria)) {
		printf(WHT "[Memoria] " RESET "Se pudo asignar correctamente una variable.\n" RESET);
	} else {
		printf(WHT "[Memoria] " RESET RED "Fallecio feo la asignación.\n" RESET);
	}

}

t_valor_variable obtenerValorCompartida(t_nombre_compartida variable) {
	printf("Se obtiene la variable compartida %s\n", variable);

	enviar_header(kernel.socket, OBTENER_VALOR_VARIABLE, 0);

	if(recibirAlgoDe(kernel)) {
		t_valor_variable miValor = (t_valor_variable) buffer_solicitado;
		printf("El valor de la variable compartida es %i.\n", miValor);
		return miValor;
	}

	printf(CYN "[Kernel] " RESET RED "Fallecio feo la obtención de valor compartida.\n" RESET);

	return 0;

}

t_valor_variable asignarValorCompartida(t_nombre_compartida variable, t_valor_variable valor) {
	printf("Se asigna la variable compartida %s con el valor %d.\n", variable, valor);

	Variable_Global miVariable;

	miVariable.valor = valor;
	miVariable.variable = variable;

	enviar_header(kernel.socket, ASIGNAR_VALOR_VARIABLE, sizeof(miVariable));
	send(kernel.socket, &miVariable, sizeof(miVariable), 0);

	if(recibirAlgoDe(kernel)) {
		printf("Se asigno la variable exitosamente.\n");
		return valor;
	}

	printf(CYN "[Kernel] " RESET RED "Fallecio feo la asignación de valor compartida.\n" RESET);

	return valor;
}

void irAlLabel(t_nombre_etiqueta etiqueta) { // TODO no sé si funciona
	printf("Se va al label %s.\n", etiqueta);
	actualPCB->program_counter = metadata_buscar_etiqueta(etiqueta,	actualPCB->etiquetas, actualPCB->etiquetas_size); // Tengo mis dudas con esta linea. El program_counter me hace ruido cambiarlo así, porque despues se suma encima. Medio turbiex
}

void llamarSinRetorno(t_nombre_etiqueta etiqueta) { // TODO y esto? WTF
	printf("Se llama sin retorno hacia %s\n", etiqueta);
	/*Entrada_stack *nueva_entrada = malloc(sizeof(Entrada_stack));

	// Se guarda el indice de codigo actual como posicion de retorno de la funcion
	nueva_entrada->retPos = actualPCB->program_counter;

	//list_add(nueva_entrada->args, ARGUMENTOS!!!);

	list_add(actualPCB->indice_stack, nueva_entrada);
	actualPCB->puntero_stack++;

	irAlLabel(etiqueta);*/
}

void llamarConRetorno(t_nombre_etiqueta etiqueta, t_puntero donde_retornar) { // TODO y esto? WTF
	printf("Se llama con retorno hacia %s\n", etiqueta);
	/*Entrada_stack *nueva_entrada = malloc(sizeof(Entrada_stack));

	nueva_entrada->retPos = actualPCB->program_counter;

	nueva_entrada->retVar.numero_pagina = donde_retornar / MARCO_SIZE;
	nueva_entrada->retVar.offset = donde_retornar % MARCO_SIZE;
	nueva_entrada->retVar.size = sizeof(int);		// Tamanio fijo?

	//list_add(nueva_entrada->args, ARGUMENTOS!!!);

	list_add(actualPCB->indice_stack, nueva_entrada);
	actualPCB->puntero_stack++;

	irAlLabel(etiqueta);*/
}

void finalizar(void) {
	printf("Programa finalizado correctamente.\n");

	actualPCB->exit_code = 0;
	elProgramaNoFinalizo = false;
}

void retornar(t_valor_variable retorno) {
	printf("Se retorna el valor %i.\n", retorno);

	enviar_header(kernel.socket, RETORNAR_VALOR, sizeof(retorno));

	send(kernel.socket, &retorno, sizeof(retorno), 0);

	if(recibirAlgoDe(kernel)) {
		// Habría que almacenarlo en retVar
		printf("Se pudo retornar exitosamente el valor %i.\n", retorno);
	} else {
		printf(CYN "[Kernel] " RESET RED "No se pudo retornar correctamente el valor." RESET);
	}

}

/*
 * ↑ Parsear tranqui ↑
 */

/*
 * ↓ Parsear Kernel ↓
 */

void wait(t_nombre_semaforo identificador_semaforo) { // TODO

}

void parser_signal(t_nombre_semaforo identificador_semaforo) { // TODO

}

t_puntero reservar(t_valor_variable espacio) { // TODO
	return 1;
}

void liberar(t_puntero puntero) { // TODO

}

t_descriptor_archivo abrir(t_direccion_archivo direccion, t_banderas flags) { // TODO
	return 1;
}

void borrar(t_descriptor_archivo direccion) { // TODO

}

void cerrar(t_descriptor_archivo descriptor_archivo) { // TODO

}

void moverCursor(t_descriptor_archivo descriptor_archivo, t_valor_variable posicion) { // TODO

}

void escribir(t_descriptor_archivo descriptor_archivo, void* informacion, t_valor_variable tamanio) { // TODO

}

void leer(t_descriptor_archivo descriptor_archivo, t_puntero informacion, t_valor_variable tamanio) { // TODO

}

/*
 * ↑ Parsear Kernel ↑
 */
