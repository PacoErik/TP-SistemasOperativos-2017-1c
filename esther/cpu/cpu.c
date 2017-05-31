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
#define FINALIZO_CORRECTAMENTE 0;
#define NO_SE_PUDIERON_RESERVAR_RECURSOS -1;
#define ARCHIVO_NO_EXISTE -2;
#define INTENTO_LEER_SIN_PERMISOS -3;
#define INTENTO_ESCRIBIR_SIN_PERMISOS -4;
#define EXCEPCION_MEMORIA -5;
#define DESCONEXION_CONSOLA -6;
#define COMANDO_FINALIZAR_PROGRAMA -7;
#define INTENTO_RESERVAR_MAS_MEMORIA_QUE_PAGINA -8;
#define NO_SE_PUEDEN_ASIGNAR_MAS_PAGINAS -9;
#define EXCEPCION_KERNEL -10;
#define SIN_DEFINICION -20;

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
#define GRN   "\x1B[32m"	// Finalización correcta del programa (EXIT CODE == 1).
#define YEL   "\x1B[33m"	// Finalización incorrecta del programa (EXIT CODE != 1).
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

PCB *actualPCB; // Programa corriendo
Posicion_memoria actualPosicion; // Actual posicion de memoria a utilizar
posicionDeMemoriaAPedir actualPosicionVariable; // Usado para comunicarse con el proceso memoria

int actualValorVariable;

int MARCO_SIZE;

void establecerConfiguracion();
void configurar(char*);
void logearInfo(char *, ...);
void logearError(char *, int, ...);
void conectarA(char* IP, int PUERTO, char identificador);
int cumplirDeseosDeKernel(char codigoDeOperacion, unsigned short bytesDePayload);
int cumplirDeseosDeMemoria(char codigoDeOperacion, unsigned short bytesDePayload);
void solicitarPCB();
void devolverPCB();
void finalizarPrograma();
void obtenerPCB(unsigned short bytesDePayload);
void comenzarTrabajo();
void* serializar_PCB(PCB *pcb, int* buffersize);
PCB *deserializar_PCB(void *buffer);
void ejecutarInstruccion();
void obtenerInstruccionDeMemoria();
void solicitarInstruccion();
bool elProgramaEstaASalvo();
int pedirMemoria();
void agregarAlStack(t_nombre_variable identificador_variable, Posicion_memoria posicionDeMemoria);
int analizarHeader(servidor servidor, headerDeLosRipeados header);
void obtenerPosicionDeMemoria();
int recibirAlgoDe(servidor servidor);
void leerMensaje(servidor servidor, unsigned short bytesDePayload);
void serializarposicionDeMemoriaAPedir(posicionDeMemoriaAPedir *posicion, char *mensajePosicion);
void deserializarposicionDeMemoriaAPedir(posicionDeMemoriaAPedir *posicion, char *mensajePosicion);
void obtenerValorVariable();
void obtenerMarcoSize();
t_puntero calcularPuntero(Posicion_memoria posicion);

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
			// TODO
	}
}

/*
 * ↑ Señales ↑
 */

int main(void) {

	signal(SIGUSR1, rutinaSignal);

	configurar("cpu");

	conectarA(IP_KERNEL, PUERTO_KERNEL, KERNEL);
	conectarA(IP_MEMORIA, PUERTO_MEMORIA, MEMORIA);

	handshake(kernel.socket, CPU);
	handshake(memoria.socket, CPU);

	recibirAlgoDe(memoria); // Recibir el MARCO_SIZE

	printf(MAG "[CPU] " RESET "Comenzando el trabajo.\n");

	comenzarTrabajo();

	return 1;
}

/*
 * ↓ Trabajar ↓
 */

void comenzarTrabajo() {
	for(;;) {
		programaVivitoYColeando = true;
		elProgramaNoFinalizo = true;
		solicitarPCB();
		solicitarInstruccion();
		ejecutarInstruccion();
		devolverPCB();
	}
}

void solicitarPCB() {
	recibirAlgoDe(kernel);
}

void solicitarInstruccion() {
	if(!elProgramaEstaASalvo()) {
		return;
	}

	posicionDeMemoriaAPedir instruccion;
	t_intructions instruction = actualPCB->instrucciones_serializado[actualPCB->program_counter];

	instruccion.processID = actualPCB->pid;
	//instruccion.numeroDePagina = actualPCB.
	instruccion.size = instruction.offset;
	instruccion.offset = instruction.start;

	headerDeLosRipeados header;
	header.codigoDeOperacion = INSTRUCCION;
	header.bytesDePayload = sizeof(instruccion);

	send(memoria.socket, &header, sizeof(header), 0); // Le mando el header

	send(memoria.socket, &instruccion, sizeof(instruccion), 0); // Le mando la geolocalizacion de la instrucción

	// Ahora esperamos la instruccion

	recibirAlgoDe(memoria);

}

void ejecutarInstruccion() {
	if(!elProgramaEstaASalvo()) {
		return;
	}

	analizadorLinea(strdup(actualInstruccion), &funciones, &funcionesnucleo);
	free(actualInstruccion);

	// ¿? Acá pasa algo ¿?

	actualPCB->program_counter++;

	// TODO
}

void devolverPCB() {
	char codigo;

	if(elProgramaEstaASalvo()) {
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

	int PCBSize = sizeof(actualPCB);
	void *PCBComprimido = malloc(PCBSize);
	void serializarPCB(PCB *miPCB, void *PCBComprimido);

	headerDeLosRipeados header;
	header.bytesDePayload = PCBSize;
	header.codigoDeOperacion = codigo;

	send(kernel.socket, &header, sizeof(header), 0);

	send(kernel.socket, PCBComprimido, PCBSize, 0);
	free(PCBComprimido);

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
	int bytesRecibidos;
	headerDeLosRipeados header;
	int respuesta;

	do {
		bytesRecibidos = recv(servidor.socket, &header, sizeof(header), 0);

		respuesta = analizarHeader(servidor, header);

	} while(respuesta == 2); // Quiere decir que es un mensaje, hay que volver a iterar

	return respuesta;
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
			// TODO
			exit(EXIT_FAILURE);
	}

	return 0; // Hubo un problema
}

int cumplirDeseosDeKernel(char codigoDeOperacion, unsigned short bytesDePayload) {
	switch(codigoDeOperacion) {
		case MENSAJE:
			leerMensaje(kernel, bytesDePayload);
			return 2;
			break;

		case PEDIR_MEMORIA_OK:
			obtenerPosicionDeMemoria();
			break;

		case EXCEPCION_DE_SOLICITUD:
			finalizarPrograma(kernel);
			break;

		case PCB_INCOMPLETO:
			obtenerPCB(bytesDePayload);
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

		default:
			printf("TODO");
			// TODO
	}

	return 1;
}

int cumplirDeseosDeMemoria(char codigoDeOperacion, unsigned short bytesDePayload) {

	switch(codigoDeOperacion) {
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

		case OBTENER_VALOR_VARIABLE_OK:
			obtenerValorVariable();
			break;

		default:
			printf("TODO");
			// TODO
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
		// La cagó el Kernel TODO
		// Finalizo programa?
	}

/*	deserializar_PCB(&actualPCB, bufferPCB);*/
	free(bufferPCB);
}

void finalizarPrograma(servidor servidor) {
	switch(servidor.identificador) {
		case KERNEL:
			printf(RED "[Excepcion] " RESET "Debido a una excepcion de solicitud al Kernel, el proceso %i ripeo.\n", actualPCB->pid);
			actualPCB->exit_code = EXCEPCION_KERNEL; // EXIT CODE -10: Excepcion de Kernel.
			break;

		case MEMORIA:
			printf(RED "[Excepcion] " RESET "Debido a una excepcion de solicitud a la Memoria, el proceso %i ripeo.\n", actualPCB->pid);
			actualPCB->exit_code = EXCEPCION_MEMORIA;
			break;
	}

	programaVivitoYColeando = false;

}

void leerMensaje(servidor servidor, unsigned short bytesDePayload) {
    if(bytesDePayload <= 0) {
    	// Concha tuya. TODO
    }

	char* mensaje = malloc(bytesDePayload+1);
    recv(servidor.socket, mensaje, bytesDePayload, 0);
    mensaje[bytesDePayload]='\0';
//  logearInfo("Mensaje recibido: %s\n", mensaje); // En mensaje recibido estaria bueno poner el nombre del que mando el msj TODO
    printf(CYN "Kernel: " RESET "%s", mensaje);
    free(mensaje);
}

void obtenerInstruccionDeMemoria() {
	unsigned short instruccionSize;
	instruccionSize = actualPCB->instrucciones_serializado->offset - actualPCB->instrucciones_serializado->start + 1; // "+ 1" porque hay un byte que en la resta se lo come

	actualInstruccion = malloc(instruccionSize + 1); // +1 por el '\0'

	unsigned short bytesRecibidos = recv(memoria.socket, actualInstruccion, instruccionSize, 0); // Recibo la instruccion

	if(bytesRecibidos <= 0) {
		// Memoria la re concha tuya TODO
	}

	actualInstruccion[instruccionSize] = '\0'; // No sé si hara falta, pero por si las moscas lo hago je.

	// Habria que verificar que actualInstruccion contenga info? Re persecuta

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
		// TODO kernel del ort
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

/*	list_add(actualPCB->indice_stack->vars, nuevaVariable); // TODO Es un poco mas complejo el list_add, ya que es un puntero el indiceDeStack*/
}

void obtenerValorVariable() {

	int bytesRecibidos = recv(memoria.socket, &actualValorVariable, sizeof(int), 0);

	if(bytesRecibidos <= 0) {
		// TODO memoria del ort
	}

}

void obtenerMarcoSize() {
	int bytesRecibidos = recv(kernel.socket, &MARCO_SIZE, sizeof(MARCO_SIZE), 0);

	if(bytesRecibidos <= 0) {
		// TODO
	}
}

/*
 * ↑ Comunicarse con los servidores ↑
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
		logearError("No se pudo conectar a %s\n", true, quienEs);
	}

	logearInfo("Conectado a %s\n", quienEs);
	free(quienEs);
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

bool elProgramaEstaASalvo() {
	// return actualPCB.exitCode > 0; // Esto es medio cheat... En el TP aclaran que no se puede ver el exitcode, pero jej
	return programaVivitoYColeando;
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

t_puntero calcularPuntero(Posicion_memoria posicion) {
	return posicion.numero_pagina * MARCO_SIZE + posicion.offset;
}

void serializarposicionDeMemoriaAPedir(posicionDeMemoriaAPedir *posicion, char *mensajePosicion) { // TODO

}

void deserializarposicionDeMemoriaAPedir(posicionDeMemoriaAPedir *posicion, char *mensajePosicion) { // TODO

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
	printf("Se definio una variable con el caracter %c.\n", identificador_variable);

	if(pedirMemoria() == 1) {
		printf(WHT "[Memoria] " RESET "Devolvió exitosamente una posición de memoria.\n");
		agregarAlStack(identificador_variable, actualPosicion);
		return calcularPuntero(actualPosicion);
	} else {
		printf(WHT "[Memoria] " RESET RED "Acceso invalido a la memoria.\n" RESET);
		programaVivitoYColeando = false;
		actualPCB->exit_code = EXCEPCION_MEMORIA;
		return 0;
	}

}

t_puntero obtenerPosicionVariable(t_nombre_variable identificador_variable) {
	Entrada_stack *entrada = list_get(actualPCB->indice_stack, actualPCB->puntero_stack);

	_Bool mismo_identificador(void* param) {
		Variable* var = (Variable*) param;
		return identificador_variable == var->identificador;
	}

	Variable *miVariable = list_find(entrada->vars, &mismo_identificador);

	return miVariable == NULL ? -1 : calcularPuntero(miVariable->posicion);
}

t_valor_variable dereferenciar(t_puntero direccion_variable) {
	t_valor_variable miValor;
	posicionDeMemoriaAPedir posicion;

	Entrada_stack *entrada = list_get(actualPCB->indice_stack, actualPCB->puntero_stack);

	_Bool compararDireccionVariable(void* param) {
		Variable* var = (Variable*) param;
		return direccion_variable == calcularPuntero(var->posicion);
	}

	Variable *miVariable = list_find(entrada->vars, &compararDireccionVariable);

	// Hay que obtener la posicion de la variable gracias a nuestro MARCO_SIZE

	posicion.processID = actualPCB->pid;
	posicion.numero_pagina = miVariable->posicion.numero_pagina;
	posicion.offset = miVariable->posicion.offset;
	posicion.size = miVariable->posicion.size;

	headerDeLosRipeados header;
	header.codigoDeOperacion = OBTENER_VALOR_VARIABLE;
	header.bytesDePayload = sizeof(posicion);

	send(memoria.socket, &header, sizeof(header), 0);
	send(memoria.socket, &posicion, sizeof(posicion), 0);

	if (recibirAlgoDe(memoria) == 1) {
		printf(WHT "[Memoria] " RESET "Devolvió exitosamente un valor de la memoria.\n");
		return miValor;
	} else {
		printf(WHT "[Memoria] " RESET RED "Acceso invalido a la memoria.\n" RESET);
		programaVivitoYColeando = false;
		actualPCB->exit_code = EXCEPCION_MEMORIA;
		return 0; // TODO Al ripear hay que ver que devolver, ya que tranquilamente puede no ripear y estar en el offset 0.
	}
}

void asignar(t_puntero direccion_variable, t_valor_variable valor) { // TODO
	printf("Se asigno una variable con el valor %i\n", valor);

	// Enviar la operacion ASIGNAR_VALOR_VARIABLE a la memoria.
}

t_valor_variable obtenerValorCompartida(t_nombre_compartida variable) { // TODO
	// Pregunta el valor al kernel
	return 1;
}

t_valor_variable asignarValorCompartida(t_nombre_compartida variable, t_valor_variable valor) { // TODO
	// Envia un pedido al kernel
	return 1;
}

void irAlLabel(t_nombre_etiqueta etiqueta) {
	actualPCB->program_counter = metadata_buscar_etiqueta(etiqueta,
			actualPCB->etiquetas, actualPCB->etiquetas_size);
}

void llamarSinRetorno(t_nombre_etiqueta etiqueta) {
	Entrada_stack *nueva_entrada = malloc(sizeof(Entrada_stack));

	/* Se guarda el indice de codigo actual como posicion de retorno de la funcion */
	nueva_entrada->retPos = actualPCB->program_counter;

	//list_add(nueva_entrada->args, ARGUMENTOS!!!);

	list_add(actualPCB->indice_stack, nueva_entrada);
	actualPCB->puntero_stack++;

	irAlLabel(etiqueta);
}

void llamarConRetorno(t_nombre_etiqueta etiqueta, t_puntero donde_retornar) {
	Entrada_stack *nueva_entrada = malloc(sizeof(Entrada_stack));

	nueva_entrada->retPos = actualPCB->program_counter;

	nueva_entrada->retVar.numero_pagina = donde_retornar / MARCO_SIZE;
	nueva_entrada->retVar.offset = donde_retornar % MARCO_SIZE;
	nueva_entrada->retVar.size = sizeof(int);		// Tamanio fijo?

	//list_add(nueva_entrada->args, ARGUMENTOS!!!);

	list_add(actualPCB->indice_stack, nueva_entrada);
	actualPCB->puntero_stack++;

	irAlLabel(etiqueta);
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

void finalizar(void) {
	if (actualPCB->puntero_stack != 0) {
		Entrada_stack *entrada = list_remove(actualPCB->indice_stack, actualPCB->puntero_stack);

		actualPCB->puntero_stack--;
		actualPCB->program_counter = entrada->retPos;

		destruir_entrada_stack(entrada);
	}
	else {
		// Finalizar programa
		actualPCB->exit_code = 0;
		elProgramaNoFinalizo = false;
	}
}

void retornar(t_valor_variable retorno) {
	Entrada_stack *entrada = list_remove(actualPCB->indice_stack, actualPCB->puntero_stack);

	actualPCB->puntero_stack--;
	actualPCB->program_counter = entrada->retPos;

	//asignar(calcularPuntero(entrada->retVar), VALOR_VARIABLE_A_RETORNAR!!!!);
	destruir_entrada_stack(entrada);
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
