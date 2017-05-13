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
#include "commons/collections/list.h"
#include "parser/parser.h"

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
void irAlLabel(t_nombre_etiqueta t_nombre_etiqueta);
void llamarSinRetorno(t_nombre_etiqueta etiqueta);
void llamarConRetorno(t_nombre_etiqueta etiqueta, t_puntero donde_retornar);
void finalizar(void);
void retornar(t_valor_variable retorno);

// *************** Funciones nucleo
void wait(t_nombre_semaforo identificador_semaforo);
void signal(t_nombre_semaforo identificador_semaforo);
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
		.AnSISOP_signal = signal,
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

enum CodigoDeOperacion {
	MENSAJE, // Mensaje a leer
	CONSOLA, MEMORIA, FILESYSTEM, CPU, KERNEL, // Identificador de un proceso
	EXCEPCION_DE_SOLICITUD, INSTRUCCION, PCB_INCOMPLETO, PEDIR_MEMORIA_VARIABLE, // Mensajes provenientes de procesos varios
	ABRIR_ARCHIVO, LEER_ARCHIVO, ESCRIBIR_ARCHIVO, CERRAR_ARCHIVO, // Mensajes provenientes de Kernel
	PCB_COMPLETO, PCB_EXCEPCION, // Mensajes provenientes de la CPU
	PEDIR_MEMORIA_OK // Mensajes provenientes de la memoria
};

typedef struct headerDeLosRipeados {
	unsigned short bytesDePayload;
	char codigoDeOperacion; // 0 (mensaje). Handshake: 1 (consola), 2 (memoria), 3 (filesystem), 4 (cpu), 5 (kernel). 6 en adelante son códigos de distintos procesos.
}__attribute__((packed, aligned(1))) headerDeLosRipeados;

char IP_KERNEL[16]; // 255.255.255.255 = 15 caracteres + 1 ('\0')
int PUERTO_KERNEL;
char IP_MEMORIA[16];
int PUERTO_MEMORIA;

typedef struct indiceCodigo {
	char numeroDePagina;
	char offset;
	char size;
}__attribute__((packed, aligned(1))) indiceCodigo;

typedef struct indiceEtiquetas {
	// Diccionario what the actual fuck
}__attribute__((packed, aligned(1))) indiceEtiquetas;

typedef struct posicionDeMemoria { // Esto es del stack
	char numeroDePagina;
	char offset;
	char size;
}__attribute__((packed, aligned(1))) posicionDeMemoria;

typedef struct argumento {
	char identificador;
	/*
	char numeroDePagina;
	char offset;
	char size;
	*/
	t_puntero posicionDeMemoria;
}__attribute__((packed, aligned(1))) argumento;

typedef struct variable {
	char identificador;
	/*
	char numeroDePagina;
	char offset;
	char size;
	*/
	t_puntero posicionDeMemoria;
}__attribute__((packed, aligned(1))) variable;

typedef struct indiceStack {
	t_list listaDeArgumentos; // Esta bien el tipo?
	t_list listaDeVariables; // Esta bien el tipo?
	char direccionDeRetorno;
	t_puntero posicionDeLaVariableDeRetorno;
	// posicionDeMemoria posicionDeLaVariableDeRetorno;
}__attribute__((packed, aligned(1))) indiceStack;

typedef struct PCB {
	char processID;
	char programCounter;
	char paginasDeCodigo;
	indiceCodigo indiceDeCodigo;
	indiceEtiquetas indiceDeEtiquetas;
	indiceStack indiceDeStack;
	int8_t exitCode;
}__attribute__((packed, aligned(1))) PCB;

typedef struct instruccionAPedir {
	char processID;
	char numeroDePagina;
	char offset;
	char size;
}__attribute__((packed, aligned(1))) instruccionAPedir;

typedef struct servidor { // Esto es más que nada una cheteada para poder usar al socket/identificador como constante en los switch
	int socket;
	char identificador;
} servidor;

servidor kernel;
servidor memoria;

bool programaVivitoYColeando; // Si el programa fallecio o no
bool elProgramaNoFinalizo; // Si el programa finalizo correctamente o sigue en curso

char* actualInstruccion; // Lo modelo como variable global porque se me hace menos codigo analizar los header y demas.

PCB actualPCB; // Programa corriendo

void establecerConfiguracion();
void configurar(char*);
void handshake(int, char);
void serializarHeader(headerDeLosRipeados *, void *);
void deserializarHeader(headerDeLosRipeados *, void *);
void logearInfo(char *, ...);
void logearError(char *, int, ...);
void conectarA(char* IP, int PUERTO, char identificador);
t_puntero cumplirDeseosDeKernel(char codigoDeOperacion, unsigned short bytesDePayload);
t_puntero cumplirDeseosDeMemoria(char codigoDeOperacion, unsigned short bytesDePayload);
void solicitarPCB();
void devolverPCB();
void finalizarPrograma();
void obtenerPCB(unsigned short bytesDePayload);
void comenzarTrabajo();
void serializarPCB(PCB *miPCB, void *bufferPCB);
void deserializarPCB(PCB *miPCB, void *bufferPCB);
void ejecutarInstruccion();
void serializarPosicionInstruccion(instruccionAPedir *instruccion, void *buffer);
void deserializarPosicionInstruccion(indiceCodigo *instruccion, void *buffer);
void obtenerInstruccionDeMemoria();
void solicitarInstruccion();
bool elProgramaEstaASalvo();
t_puntero pedirMemoria();
void agregarAlStack(t_nombre_variable identificador_variable, t_puntero posicionEnMemoria);
t_puntero analizarHeader(servidor servidor, void* bufferHeader);
t_puntero obtenerPosicionDeMemoria();
t_puntero recibirAlgoDe(servidor servidor);
void leerMensaje(servidor servidor, unsigned short bytesDePayload);


int main(void) {

	configurar("cpu");

	conectarA(IP_KERNEL, PUERTO_KERNEL, KERNEL);
	conectarA(IP_MEMORIA, PUERTO_MEMORIA, MEMORIA);

	handshake(kernel.socket, CPU);
	handshake(memoria.socket, CPU);

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

	instruccionAPedir instruccion;
	instruccion.processID = actualPCB.processID;
	instruccion.numeroDePagina = actualPCB.indiceDeCodigo.numeroDePagina;
	instruccion.offset = actualPCB.indiceDeCodigo.offset;
	instruccion.size = actualPCB.indiceDeCodigo.size;

	char* bufferInstruccion;
	bufferInstruccion = malloc(sizeof(instruccion));

	serializarPosicionInstruccion(&instruccion, bufferInstruccion);
	unsigned short instruccionSize = instruccion.size;

	headerDeLosRipeados header;
	header.codigoDeOperacion = INSTRUCCION;
	header.bytesDePayload = instruccionSize;

	char* bufferHeader;
	bufferHeader = malloc(sizeof(header));

	serializarHeader(&header, bufferHeader);
	int bufferHeaderSize = sizeof(headerDeLosRipeados);
	send(memoria.socket, bufferHeader, bufferHeaderSize, 0); // Le mando el header

	send(memoria.socket, bufferInstruccion, instruccionSize, 0); // Le mando la geolocalizacion de la instrucción

	free(bufferInstruccion);

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

	actualPCB.programCounter++;

	// TODO
}

void devolverPCB() {
	char codigo;

	if(elProgramaEstaASalvo()) {
		if(elProgramaNoFinalizo) {
			codigo = PCB_INCOMPLETO;
		} else {
			codigo = PCB_COMPLETO;
		}
	} else {
		codigo = PCB_EXCEPCION;
	}

	int PCBSize = sizeof(actualPCB);
	void *PCBComprimido = malloc(PCBSize);
	void serializarPCB(PCB *miPCB, void *PCBComprimido);

	headerDeLosRipeados headerDeMiMensaje;
	headerDeMiMensaje.bytesDePayload = PCBSize;
	headerDeMiMensaje.codigoDeOperacion = codigo;
	int headerSize = sizeof(headerDeMiMensaje);
	void *headerComprimido = malloc(headerSize);
	serializarHeader(&headerDeMiMensaje, headerComprimido);

	send(kernel.socket, headerComprimido, headerSize, 0);
	free(headerComprimido);

	send(kernel.socket, PCBComprimido, PCBSize, 0);
	free(PCBComprimido);

	// limpiarActualPCB(); No haría realmente falta.. Pero sería más realista
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
/*
int analizarHeader(servidor servidor) {
	int bufferHeaderSize = sizeof(headerDeLosRipeados);
	void *bufferHeader = malloc(bufferHeaderSize);
    int bytesRecibidos = recv(servidor.socket, bufferHeader, bufferHeaderSize, 0);

	if (bytesRecibidos <= 0) {
		return -1;
	}

    headerDeLosRipeados header;
    deserializarHeader(&header, bufferHeader);
    free(bufferHeader);

    if (header.codigoDeOperacion == MENSAJE) {
        if (header.bytesDePayload <= 0) {
            logearError("El cliente %i intentó mandar un mensaje sin contenido\n", false, servidor.socket);
            return -2;
        }
        else {
            leerMensaje(servidor, header.bytesDePayload);
        }
    } else {
    	analizarCodigosDeOperacion(servidor, header.codigoDeOperacion, header.bytesDePayload);
    }

    return 1;
}
*/

t_puntero recibirAlgoDe(servidor servidor) {
	int bufferHeaderSize = sizeof(headerDeLosRipeados);
	void *bufferHeader = malloc(bufferHeaderSize);
	unsigned int bytesRecibidos;

	t_puntero respuesta;

	do {

		do {
			bytesRecibidos = recv(servidor.socket, bufferHeader, bufferHeaderSize, 0);
		} while(bytesRecibidos <= 0); // Algo anda mal, segui esperando el mensaje

		respuesta = analizarHeader(servidor, bufferHeader);

	} while(respuesta == 2); // Quiere decir que es un mensaje, hay que volver a iterar

	return respuesta;
}

t_puntero analizarHeader(servidor servidor, void* bufferHeader) {
	headerDeLosRipeados header;
	deserializarHeader(&header, bufferHeader);
	free(bufferHeader);

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

t_puntero cumplirDeseosDeKernel(char codigoDeOperacion, unsigned short bytesDePayload) {
	switch(codigoDeOperacion) {
		case MENSAJE:
			leerMensaje(kernel, bytesDePayload);
			return 2;
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

t_puntero cumplirDeseosDeMemoria(char codigoDeOperacion, unsigned short bytesDePayload) {
	t_puntero posicionEnMemoria;

	switch(codigoDeOperacion) {
	/*
	 * No nos va a interesar que nos mande un msj.
	 *
		case MENSAJE:
			leerMensaje(memoria, bytesDePayload);
			break;
	*/
		case EXCEPCION_DE_SOLICITUD:
			finalizarPrograma(memoria);
			return 0;
			break;

		case INSTRUCCION:
			obtenerInstruccionDeMemoria();
			break;

		case PEDIR_MEMORIA_OK:
			return obtenerPosicionDeMemoria();
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

    PCB miPCB;
	deserializarPCB(&miPCB, bufferPCB);
	free(bufferPCB);

	actualPCB = miPCB;
}

void finalizarPrograma(servidor servidor) {
	switch(servidor.identificador) {
		case KERNEL:
			printf("Debido a una excepcion de solicitud al Kernel, el proceso %c ripeo", actualPCB.processID);
			actualPCB.exitCode = EXCEPCION_KERNEL; // EXIT CODE -10: Excepcion de Kernel.
			break;

		case MEMORIA:
			printf("Debido a una excepcion de solicitud a la Memoria, el proceso %c ripeo", actualPCB.processID);
			actualPCB.exitCode = EXCEPCION_MEMORIA;
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
    logearInfo("Mensaje recibido: %s\n", mensaje); // En mensaje recibido estaria bueno poner el nombre del que mando el msj TODO
    free(mensaje);
}

void obtenerInstruccionDeMemoria() {
	unsigned short instruccionSize;
	instruccionSize = actualPCB.indiceDeCodigo.size;

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

/*
char* pedirInstruccionAMemoria() {
	indiceCodigo instruccion;
	instruccion.numeroDePagina = actualPCB.indiceDeCodigo.numeroDePagina;
	instruccion.offset = actualPCB.indiceDeCodigo.offset;
	instruccion.size = actualPCB.indiceDeCodigo.size;

	char* bufferInstruccion;
	bufferInstruccion = malloc(sizeof(instruccion));

	serializarPosicionInstruccion(&instruccion, bufferInstruccion);
	unsigned short instruccionSize = sizeof(bufferInstruccion);

	headerDeLosRipeados header;
	header.codigoDeOperacion = INSTRUCCION;
	header.bytesDePayload = instruccionSize;

	char* bufferHeader;
	bufferHeader = malloc(sizeof(header));

	serializarHeader(&header, bufferHeader);
	char headerSize = sizeof(bufferHeader);
	send(memoria.socket, bufferHeader, headerSize, 0); // Le mando el header

	send(memoria.socket, bufferInstruccion, instruccionSize, 0); // Le mando la geolocalizacion de la instrucción

	free(bufferInstruccion);

	// Luego de un tiempo

	recv(memoria.socket, bufferHeader, headerSize, 0); // Recibo el header

	// Revisar que envio el correcto cod. de op. y que no hubo problemas TODO

	deserializarHeader(&header, bufferHeader);

	free(bufferHeader);

	instruccionSize = header.bytesDePayload;
	bufferInstruccion = malloc(instruccionSize + 1); // +1 por el '/0'

	recv(memoria.socket, bufferInstruccion, instruccionSize, 0); // Recibo la instruccion

	// Revisar que no hubo problemas

	return bufferInstruccion;

}
*/

t_puntero pedirMemoria() {

	headerDeLosRipeados miHeader;
	miHeader.codigoDeOperacion = PEDIR_MEMORIA_VARIABLE;
	miHeader.bytesDePayload = actualPCB.processID; // Acá pongo el ID del proceso que pide memoria

	char* bufferHeader;
	char bufferHeaderSize = sizeof(miHeader);
	bufferHeader = malloc(bufferHeaderSize);

	serializarHeader(&miHeader, &bufferHeader);

	send(memoria.socket, bufferHeader, bufferHeaderSize, 0);

	// Ahora esperemos el ok

	t_puntero posicionEnMemoria = recibirAlgoDe(memoria);

	return posicionEnMemoria;

}

t_puntero obtenerPosicionDeMemoria() {
	t_puntero posicionMemoria;

	char* mensajePosicion;
	mensajePosicion = malloc(sizeof(t_puntero));

	unsigned int bytesRecibidos;

	bytesRecibidos = recv(memoria.socket, mensajePosicion, sizeof(t_puntero), 0);

	if(bytesRecibidos <= 0) {
		// TODO memoria del ort
	}

	posicionMemoria = atoi(mensajePosicion);

	return posicionMemoria;
}

void agregarAlStack(t_nombre_variable identificador_variable, t_puntero posicionEnMemoria) {
	variable nuevaVariable;

	nuevaVariable.identificador = identificador_variable;
	nuevaVariable.posicionDeMemoria = posicionEnMemoria;

	list_add(&actualPCB.indiceDeStack.listaDeVariables, &nuevaVariable);
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

void serializarPCB(PCB *miPCB, void *bufferPCB) { // TODO
	// hora de chinear
}

void deserializarPCB(PCB *miPCB, void *bufferPCB) { // TODO
	// hora de chinear
}

void serializarPosicionInstruccion(instruccionAPedir *instruccion, void *buffer) { // TODO

}

void deserializarPosicionInstruccion(indiceCodigo *instruccion, void *buffer) { // TODO

}

/*
 * ↑ Funciones auxiliares del CPU ↑
 */

/*
 * ↓ Parsear tranqui ↓
 */

t_puntero definirVariable(t_nombre_variable identificador_variable) {
	printf("Se definio una variable con el caracter %c", identificador_variable);

	t_puntero posicionEnMemoria;
	posicionEnMemoria = pedirMemoria();

	if(posicionEnMemoria == 0) { // Se rompio en algun lado
		programaVivitoYColeando = false;
		actualPCB.exitCode = EXCEPCION_MEMORIA;
		return posicionEnMemoria;
	}

	agregarAlStack(identificador_variable, posicionEnMemoria);

	return posicionEnMemoria;
}

t_puntero obtenerPosicionVariable(t_nombre_variable identificador_variable) { // TODO
	return 1;
}

t_valor_variable dereferenciar(t_puntero direccion_variable) { // TODO
	return 1;
}

void asignar(t_puntero direccion_variable, t_valor_variable valor) { // TODO
	printf("Se asigno una variable con el valor %i", valor);

	// Segun lo que tengo entendido, direccion_variable es la direccion en el proceso memoria.
	// Muy automagico.


}

t_valor_variable obtenerValorCompartida(t_nombre_compartida variable) { // TODO
	return 1;
}

t_valor_variable asignarValorCompartida(t_nombre_compartida variable, t_valor_variable valor) { // TODO
	return 1;
}

void irAlLabel(t_nombre_etiqueta t_nombre_etiqueta) { // TODO

}

void llamarSinRetorno(t_nombre_etiqueta etiqueta) { // TODO

}

void llamarConRetorno(t_nombre_etiqueta etiqueta, t_puntero donde_retornar) { // TODO

}

void finalizar(void) {
	actualPCB.exitCode = 0;
	elProgramaNoFinalizo = false;
}

void retornar(t_valor_variable retorno) { // TODO

}

/*
 * ↑ Parsear tranqui ↑
 */

/*
 * ↓ Parsear Kernel ↓
 */

void wait(t_nombre_semaforo identificador_semaforo) { // TODO

}

void signal(t_nombre_semaforo identificador_semaforo) { // TODO

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
