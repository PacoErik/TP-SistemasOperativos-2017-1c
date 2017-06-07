//-----HEADERS-----//
#include "qepd/qepd.h"
#include "commons/string.h"
#include <pthread.h>
#include <signal.h>
#include "commons/collections/list.h"
#include "parser/metadata_program.h"
#include <time.h>


//-----DEFINES-----//
#define RUTA_CONFIG "config.cfg"
#define RUTA_LOG "cpu.log"

#define VIVITO_Y_COLEANDO 1; // No se pueden revisar los exit code previo a que ripee

#define RED   "\x1B[31m"	// Señales, excepciones.
#define BLU   "\x1B[34m"	// Finalizacion de una rafaga del programa.
#define GRN   "\x1B[32m"	// Finalización correcta del programa (EXIT CODE == 1 o 0).
#define YEL   "\x1B[33m"	// Finalización incorrecta del programa (EXIT CODE < 0).
#define MAG   "\x1B[35m"	// Comienzo y finalización del proceso.
#define CYN   "\x1B[36m"	// Mensaje proveniente del Kernel.
#define WHT   "\x1B[37m"	// Acceso a memoria.
#define RESET "\x1B[0m"		// RESET

//-----ESTRUCTURAS-----//
typedef struct Posicion_memoria {
	int numero_pagina;
	int offset;			// Desplazamiento dentro de la pagina
	int size;
} Posicion_memoria;
typedef struct Variable {
	char identificador;
	Posicion_memoria posicion;
} PACKED Variable;
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

//-----VARIABLES GLOBALES-----//
t_log* logger;
t_config* config;

char IP_KERNEL[16];
int PUERTO_KERNEL;
char IP_MEMORIA[16];
int PUERTO_MEMORIA;
servidor kernel;
servidor memoria;

bool programaVivitoYColeando; 	// Si el programa fallecio o no
bool elProgramaNoFinalizo; 		// Si el programa finalizo correctamente o sigue en curso
bool signal_recibida = false; 	// Si se recibe o no una señal SIGUSR1

void *buffer_solicitado = NULL;

PCB *PCB_actual = NULL; // Programa corriendo
Posicion_memoria actual_posicion; // Actual posicion de memoria a utilizar
posicionDeMemoriaAPedir actual_posicion_variable; // Usado para comunicarse con el proceso memoria

int MARCO_SIZE;
int quantum;
int quantum_sleep;
int tipo_devolucion = PCB_INCOMPLETO;


//-----PROTOTIPOS Y ESTRUCTURAS AnSISOP-----//
t_puntero definir_variable(t_nombre_variable identificador_variable);
t_puntero obtener_posicion_variable(t_nombre_variable nombre);
t_valor_variable dereferenciar(t_puntero direccion_variable);
void asignar(t_puntero direccion_variable, t_valor_variable valor);
t_valor_variable obtener_valor_compartida(t_nombre_compartida variable);
t_valor_variable asignar_valor_compartida(t_nombre_compartida variable, t_valor_variable valor);
void ir_al_label(t_nombre_etiqueta etiqueta);
void llamar_sin_retorno(t_nombre_etiqueta etiqueta);
void llamar_con_retorno(t_nombre_etiqueta etiqueta, t_puntero donde_retornar);
void finalizar(void);
void retornar(t_valor_variable valor_retorno);

void kernel_wait(t_nombre_semaforo identificador_semaforo);
void kernel_signal(t_nombre_semaforo identificador_semaforo);
t_puntero reservar(t_valor_variable espacio);
void liberar(t_puntero puntero);
t_descriptor_archivo abrir(t_direccion_archivo direccion, t_banderas flags);
void borrar(t_descriptor_archivo direccion);
void cerrar(t_descriptor_archivo descriptor_archivo);
void mover_cursor(t_descriptor_archivo descriptor_archivo, t_valor_variable posicion);
void escribir(t_descriptor_archivo descriptor_archivo, void* informacion, t_valor_variable tamanio);
void leer(t_descriptor_archivo descriptor_archivo, t_puntero informacion, t_valor_variable tamanio);

AnSISOP_funciones funciones = {
		.AnSISOP_definirVariable			= definir_variable,
		.AnSISOP_obtenerPosicionVariable	= obtener_posicion_variable,
		.AnSISOP_dereferenciar				= dereferenciar,
		.AnSISOP_asignar					= asignar,
		.AnSISOP_obtenerValorCompartida		= obtener_valor_compartida,
		.AnSISOP_asignarValorCompartida		= asignar_valor_compartida,
		.AnSISOP_irAlLabel					= ir_al_label,
		.AnSISOP_llamarSinRetorno			= llamar_sin_retorno,
		.AnSISOP_llamarConRetorno			= llamar_con_retorno,
		.AnSISOP_finalizar					= finalizar,
		.AnSISOP_retornar					= retornar
};

AnSISOP_kernel funcionesnucleo = {
		.AnSISOP_wait	= kernel_wait,
		.AnSISOP_signal = kernel_signal,
		.AnSISOP_reservar = reservar,
		.AnSISOP_liberar = liberar,
		.AnSISOP_abrir = abrir,
		.AnSISOP_cerrar = cerrar,
		.AnSISOP_borrar = borrar,
		.AnSISOP_moverCursor = mover_cursor,
		.AnSISOP_escribir = escribir,
		.AnSISOP_leer = leer
};

//-----PROTOTIPOS DE FUNCIONES-----//
int 		analizar_header(servidor, headerDeLosRipeados);
int 		cumplir_deseos_kernel(char, unsigned short);
int 		cumplir_deseos_memoria(char, unsigned short);
int 		pedir_memoria();
int 		recibir_algo_de(servidor);

PCB*		deserializar_PCB(void*);

t_puntero 	calcular_puntero(Posicion_memoria);

void 		destruir_actualPCB(void);
void 		destruir_entrada_stack(void*);
void 		devolver_PCB();
void 		ejecutar_instruccion();
void 		establecer_configuracion();
void 		leer_mensaje(servidor, unsigned short);
void 		obtener_PCB(unsigned short);
void 		obtener_posicion_memoria();
void 		rutina_signal(int);
void 		solicitar_instruccion();
void 		terminar_ejecucion(int);
void		trabajar();
void* 		serializar_PCB(PCB*, int*);

//-----PROCEDIMIENTO PRINCIPAL-----//
int main(void) {

	signal(SIGUSR1, rutina_signal);

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

	while (true) {
		recibir_algo_de(kernel);
	}
	return 1;
}

//-----DEFINICIÓN DE FUNCIONES-----//

//MENSAJES
int analizar_header(servidor servidor, headerDeLosRipeados header) {
	switch(servidor.identificador) {
		case KERNEL:
			return cumplir_deseos_kernel(header.codigoDeOperacion, header.bytesDePayload);
			break;

		case MEMORIA:
			return cumplir_deseos_memoria(header.codigoDeOperacion, header.bytesDePayload);
			break;

		default:
			logear_error("Servidor desconocido, finalizando CPU...", true);
	}
	return 0;
}
int cumplir_deseos_kernel(char operacion, unsigned short bytes_payload) {
	switch(operacion) {
		case MENSAJE:
			leer_mensaje(kernel, bytes_payload);
			break;

		case QUANTUM:
			recv(kernel.socket, &quantum, sizeof(quantum), 0);
			break;

		case QUANTUM_SLEEP:
			recv(kernel.socket, &quantum_sleep, sizeof(quantum_sleep), 0);
			break;

		case PCB_INCOMPLETO:
			obtener_PCB(bytes_payload);
			trabajar();
			break;

		case OBTENER_VALOR_VARIABLE:
			free(buffer_solicitado);
			buffer_solicitado = malloc(bytes_payload);
			recv(kernel.socket, buffer_solicitado, bytes_payload, 0);
			break;

		case ASIGNAR_VALOR_VARIABLE:
			//Mensaje de confirmación
			break;

		case EXCEPCION:;
			int numero_excepcion;
			recv(kernel.socket, &numero_excepcion, sizeof(int), 0);
			terminar_ejecucion(numero_excepcion);
			return 0;
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
			printf(RED "CÓDIGO DE OPERACIÓN DESCONOCIDO POR PARTE DEL KERNEL: %i.\n" RESET, operacion);
			exit(EXIT_FAILURE);
	}

	return 1;
}
int cumplir_deseos_memoria(char operacion, unsigned short bytes_payload) {
	switch(operacion) {
		case SOLICITAR_BYTES:
			free(buffer_solicitado);
			buffer_solicitado = malloc(bytes_payload);
			recv(memoria.socket, buffer_solicitado, bytes_payload, 0);
			break;
		case ALMACENAR_BYTES:
			//Esto es simplemente un mensaje de confirmación
			//ya que no se dió una excepción
			break;
		case EXCEPCION:;
			int numero_excepcion;
			recv(memoria.socket, &numero_excepcion, sizeof(int), 0);
			terminar_ejecucion(numero_excepcion);
			return 0;
			break;
		default:
			printf(RED "CÓDIGO DE OPERACIÓN DESCONOCIDO POR PARTE DE LA MEMORIA: %i.\n" RESET, operacion);
			exit(EXIT_FAILURE);
	}

	return 1;
}
void leer_mensaje(servidor servidor, unsigned short bytes_payload) {
	char* mensaje = malloc(bytes_payload+1);
    recv(servidor.socket, mensaje, bytes_payload, 0);
    mensaje[bytes_payload]='\0';
    printf(CYN "Kernel: " RESET "%s\n", mensaje);
    free(mensaje);
}
int recibir_algo_de(servidor servidor) {
	headerDeLosRipeados header;
	int bytes_recibidos = recibir_header(servidor.socket, &header);
	if (bytes_recibidos > 0) {
		return analizar_header(servidor, header);
	} else {
		logear_error("El servidor se desconectó. Finalizando...", true);
	}
	return 0;
}

//PROCESAMIENTO
void devolver_PCB() {
	int buffersize = 0;
	void *buffer = serializar_PCB(PCB_actual, &buffersize);

	if (signal_recibida) //Le avisamos al kernel sobre la desconexión así no nos tiene en cuenta para la próxima planificación
		enviar_header(kernel.socket, DESCONEXION_CPU, 0);

	enviar_header(kernel.socket, tipo_devolucion, buffersize);
	send(kernel.socket, buffer, buffersize, 0);

	logear_info("PCB devuelto");

	free(buffer);
	destruir_actualPCB();

	if(signal_recibida) {
		printf(MAG "[CPU] " RESET "Finalización del proceso.\n");
		exit(EXIT_SUCCESS);
	}
}
void ejecutar_instruccion() {
	usleep(quantum_sleep * 1000);
	int longitud = PCB_actual->instrucciones_serializado[PCB_actual->program_counter].offset+1;
	char *instruccion_actual = malloc(longitud);
	memcpy(instruccion_actual, buffer_solicitado, longitud);
	instruccion_actual[longitud-2] = '\0';
	analizadorLinea(instruccion_actual, &funciones, &funcionesnucleo);
	free(instruccion_actual);
}
void obtener_PCB(unsigned short bytes_payload) {
	void *buffer_PCB = malloc(bytes_payload);
	int bytes_recibidos = recv(kernel.socket, buffer_PCB, bytes_payload, 0);

	if(bytes_recibidos <= 0) {
		logear_error("Kernel desconectado, finalizando CPU...", true);
	}

	PCB_actual = deserializar_PCB(buffer_PCB);

	logear_info("PCB obtenido");

	free(buffer_PCB);
}
void solicitar_instruccion() {
	logear_info("Solicitando instruccion...");

	posicionDeMemoriaAPedir posicion;
	t_intructions instruction = PCB_actual->instrucciones_serializado[PCB_actual->program_counter];

	posicion.processID = PCB_actual->pid;
	posicion.numero_pagina = 0;
	posicion.size = instruction.offset;
	posicion.offset = instruction.start;

	enviar_header(memoria.socket, SOLICITAR_BYTES, sizeof(posicion));
	send(memoria.socket, &posicion, sizeof(posicion), 0);

	// Ahora esperamos la instruccion

	recibir_algo_de(memoria); //Se asume que recibir una instrucción siempre va a salir bien
}
void trabajar() {
	tipo_devolucion = PCB_INCOMPLETO;
	programaVivitoYColeando = true;

	if (quantum == 0) {
		while (programaVivitoYColeando) {
			solicitar_instruccion();
			ejecutar_instruccion();
			PCB_actual->program_counter++;
		}
	} else {
		int i;
		for(i = 0; i < quantum; i++) {
			solicitar_instruccion();
			ejecutar_instruccion();
			PCB_actual->program_counter++;
			if(!programaVivitoYColeando) {
				break;
			}
		}
	}

	devolver_PCB(tipo_devolucion);
}

//FUNCIONES AUXILIARES
t_puntero calcular_puntero(Posicion_memoria posicion) {
	return posicion.numero_pagina * MARCO_SIZE + posicion.offset;
}
void destruir_actualPCB(void) {
	free(PCB_actual->etiquetas);
	free(PCB_actual->instrucciones_serializado);
	list_destroy_and_destroy_elements(PCB_actual->indice_stack, destruir_entrada_stack);
	free(PCB_actual);
	PCB_actual = NULL;
}
void destruir_entrada_stack(void *param) {
	Entrada_stack *entrada = (Entrada_stack *) param;
	list_destroy_and_destroy_elements(entrada->vars, free);
	list_destroy_and_destroy_elements(entrada->args, free);
	free(entrada);
}
_Bool es_parametro(char nombre) {
	return (nombre >= '0' && nombre <= '9');
}
void obtener_posicion_memoria() {

	int bytesRecibidos = recv(kernel.socket, &actual_posicion_variable, sizeof(posicionDeMemoriaAPedir), 0);

	if(bytesRecibidos <= 0) {
		printf(RED "EL KERNEL ENVÍO UNA POSICION DE MEMORIA PARA EL ORTO.\n" RESET);
		exit(EXIT_FAILURE);
	}

	actual_posicion.numero_pagina = actual_posicion_variable.numero_pagina;
	actual_posicion.offset = actual_posicion_variable.offset;
	actual_posicion.size = actual_posicion_variable.size;

}
int obtener_tamanio_stack() {
	int total = 0;
	void calcular(void* param) {
		Entrada_stack *entrada = param;
		total += 4 * (list_size(entrada->args) + list_size(entrada->vars));
	}
	list_iterate(PCB_actual->indice_stack, &calcular);
	return total;
}
int pedir_memoria() {

	headerDeLosRipeados header;
	header.codigoDeOperacion = PEDIR_MEMORIA_VARIABLE;
	header.bytesDePayload = PCB_actual->pid; // Acá pongo el ID del proceso que pide memoria

	send(kernel.socket, &header, sizeof(header), 0);

	printf(CYN "[Kernel] " RESET "Pidiendo memoria para variable.\n");

	// Ahora esperemos el ok

	return recibir_algo_de(kernel);

}
void terminar_ejecucion(int exit_code) {
	logear_error("Se finalizó la ejecución con el EXIT CODE (%d)", false, exit_code);
	programaVivitoYColeando = false;
	PCB_actual->exit_code = exit_code;
	if (exit_code < 0) {
		tipo_devolucion = PCB_EXCEPCION;
	} else if (exit_code == 0) {
		tipo_devolucion = PCB_COMPLETO;
	} else {
		tipo_devolucion = PCB_INCOMPLETO;
	}
}

//SERIALIZADORES Y DESERIALIZADORES
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

//DEFINICIÓN DE OPERACIONES
t_puntero definir_variable(t_nombre_variable identificador_variable) {
	printf("definirVariable: %c\n", identificador_variable);

	int pagina_stack_inicial = PCB_actual->cantidad_paginas_codigo;
	int tamanio_stack = obtener_tamanio_stack();
	Posicion_memoria posicion;
	posicion.numero_pagina = pagina_stack_inicial + (tamanio_stack / MARCO_SIZE);
	posicion.offset = tamanio_stack % MARCO_SIZE;
	posicion.size = 4;
	Entrada_stack *entrada_actual = list_get(PCB_actual->indice_stack, PCB_actual->puntero_stack);

	if (es_parametro(identificador_variable)) {
		Posicion_memoria *posicion_parametro = malloc(sizeof(Posicion_memoria));
		*posicion_parametro = posicion;
		list_add(entrada_actual->args, posicion_parametro);
	} else {
		Variable *variable = malloc(sizeof(Variable));
		variable->identificador = identificador_variable;
		variable->posicion = posicion;
		list_add(entrada_actual->vars, variable);
	}

	return calcular_puntero(posicion);
}
t_puntero obtener_posicion_variable(t_nombre_variable nombre) {
	if (!programaVivitoYColeando)
		return 0; //Una sola instrucción puede ejecutar muchas primitivas, si falla en la primera primitiva las otras no se dan cuenta

	printf("Obtener posicion de la variable: %c\n", nombre);

	Entrada_stack *entrada = list_get(PCB_actual->indice_stack, PCB_actual->puntero_stack);

	_Bool mismo_identificador(void* param) {
		Variable* var = (Variable*) param;
		return nombre == var->identificador;
	}

	if (es_parametro(nombre)) {
		Posicion_memoria *posicion = list_get(entrada->args, atoi(&nombre));
		if (posicion == NULL) {
			terminar_ejecucion(VARIABLE_SIN_DECLARAR);
		} else {
			return calcular_puntero(*posicion);
		}
	} else {
		Variable *variable = list_find(entrada->vars, &mismo_identificador);

		if(variable == NULL) {
			terminar_ejecucion(VARIABLE_SIN_DECLARAR);
		} else {
			return calcular_puntero(variable->posicion);
		}
	}

	return 0;
}
t_valor_variable dereferenciar(t_puntero direccion_variable) {
	printf("Dereferenciar: %i\n", direccion_variable);

	posicionDeMemoriaAPedir posicion;
	posicion.processID = PCB_actual->pid;
	posicion.numero_pagina = direccion_variable / MARCO_SIZE;
	posicion.offset = direccion_variable % MARCO_SIZE;
	posicion.size = 4;

	enviar_header(memoria.socket, SOLICITAR_BYTES, sizeof(posicion));
	send(memoria.socket, &posicion, sizeof(posicion), 0);

	if (recibir_algo_de(memoria)) {
		t_valor_variable *valor = buffer_solicitado;
		return *valor;
	}

	return 0;
}
void asignar(t_puntero direccion_variable, t_valor_variable valor) {
	if (!programaVivitoYColeando)
		return;

	posicionDeMemoriaAPedir posicion;
	posicion.processID = PCB_actual->pid;
	posicion.numero_pagina = direccion_variable / MARCO_SIZE;
	posicion.offset = direccion_variable % MARCO_SIZE;
	posicion.size = 4;

	enviar_header(memoria.socket, ALMACENAR_BYTES, sizeof(posicion));
	send(memoria.socket, &posicion, sizeof(posicion), 0);
	send(memoria.socket, &valor, sizeof(valor), 0);

	if (recibir_algo_de(memoria)) {
		printf("Se asigno una variable con el valor %i en la direccion %i.\n", valor, direccion_variable);
	}
}
t_valor_variable obtener_valor_compartida(t_nombre_compartida variable) {
	printf("Solicitando el valor de la variable compartida %s\n", variable);

	int longitud = strlen(variable)+1;
	enviar_header(kernel.socket, OBTENER_VALOR_VARIABLE, longitud);
	send(kernel.socket, variable, longitud, 0);

	if (recibir_algo_de(kernel)) {
		int *valor = buffer_solicitado;
		printf("Valor obtenido de %s = %d\n", variable, *valor);
		return *valor;
	}
	printf("No se pudo obtener el valor de %s\n", variable);
	return 0;
}
t_valor_variable asignar_valor_compartida(t_nombre_compartida variable, t_valor_variable valor) {
	printf("Asignando el valor %d a la variable compartida %s\n", valor, variable);

	int longitud = strlen(variable)+1;
	enviar_header(kernel.socket, ASIGNAR_VALOR_VARIABLE, longitud);
	send(kernel.socket, variable, longitud, 0);
	send(kernel.socket, &valor, sizeof(valor), 0);

	if (recibir_algo_de(kernel)) {
		printf("Se asignó satisfactoriamente %d a la variable %s\n", valor, variable);
		return valor;
	}
	printf("No se pudo asignar el valor a %s\n", variable);
	return 0;
}
void ir_al_label(t_nombre_etiqueta etiqueta) {
	printf("Se va al label %s\n", etiqueta);
	int nuevo_program_counter = metadata_buscar_etiqueta(etiqueta,
			PCB_actual->etiquetas, PCB_actual->etiquetas_size);
	if (nuevo_program_counter < 0) {
		//No existe la etiqueta
		terminar_ejecucion(ETIQUETA_INEXISTENTE);
	} else {
		PCB_actual->program_counter = nuevo_program_counter - 1;
		//Ese -1 místico es porque luego voy a incrementar el program counter
		//en 1, entonces se anula.. osea... -1 + 1 = 0, understand? meh
	}
}
void llamar_sin_retorno(t_nombre_etiqueta etiqueta) {
	printf("Se llama sin retorno hacia %s\n", etiqueta);
	Entrada_stack *nueva_entrada = malloc(sizeof(Entrada_stack));

	nueva_entrada->args = list_create();
	nueva_entrada->vars = list_create();

	nueva_entrada->retPos = PCB_actual->program_counter + 1;
	//Hay que volver a la instrucción siguiente de la que partí

	list_add(PCB_actual->indice_stack, nueva_entrada);
	PCB_actual->puntero_stack++;

	ir_al_label(etiqueta);
}
void llamar_con_retorno(t_nombre_etiqueta etiqueta, t_puntero donde_retornar) {
	printf("Se llama con retorno hacia %s\n", etiqueta);
	Entrada_stack *nueva_entrada = malloc(sizeof(Entrada_stack));

	nueva_entrada->args = list_create();
	nueva_entrada->vars = list_create();
	nueva_entrada->retPos = PCB_actual->program_counter + 1;
	nueva_entrada->retVar.numero_pagina = donde_retornar / MARCO_SIZE;
	nueva_entrada->retVar.offset = donde_retornar % MARCO_SIZE;
	nueva_entrada->retVar.size = 4;

	list_add(PCB_actual->indice_stack, nueva_entrada);
	PCB_actual->puntero_stack++;

	ir_al_label(etiqueta);
}
void finalizar(void) {
	printf("Finalizar.\n");
	if (PCB_actual->puntero_stack > 0) {
		Entrada_stack *entrada = list_remove(PCB_actual->indice_stack, PCB_actual->puntero_stack);

		PCB_actual->puntero_stack--;
		PCB_actual->program_counter = entrada->retPos - 1;

		destruir_entrada_stack(entrada);
	}
	else {
		terminar_ejecucion(FINALIZO_CORRECTAMENTE);
	}
}
void retornar(t_valor_variable valor_retorno) {
	printf("Se retorna el valor %i.\n", valor_retorno);

	Entrada_stack *entrada = list_remove(PCB_actual->indice_stack, PCB_actual->puntero_stack);

	//No sé si está bien esto pero en mi opinión creo que sí
	asignar(calcular_puntero(entrada->retVar), valor_retorno);

	PCB_actual->puntero_stack--;
	PCB_actual->program_counter = entrada->retPos - 1;

	destruir_entrada_stack(entrada);
}

//DEFINICIÓN DE OPERACIONES KERNEL
void kernel_wait(t_nombre_semaforo identificador_semaforo) { // TODO

}
void kernel_signal(t_nombre_semaforo identificador_semaforo) { // TODO

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
void mover_cursor(t_descriptor_archivo descriptor_archivo, t_valor_variable posicion) { // TODO

}
void escribir(t_descriptor_archivo descriptor_archivo, void* informacion, t_valor_variable tamanio) { // TODO

}
void leer(t_descriptor_archivo descriptor_archivo, t_puntero informacion, t_valor_variable tamanio) { // TODO

}

//MANEJO DE SEÑALES
void rutina_signal(int _) {
	if (PCB_actual != NULL) {
		logear_info("Señal de finalización recibida, se finalizará cuando se devuelva el PCB");
		signal_recibida = true;
	} else {
		logear_info("No estoy ejecutando nada, nos vemos!");
		exit(0);
	}
}

//EXTRA
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
