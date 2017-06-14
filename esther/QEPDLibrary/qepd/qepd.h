/*
 * qepd.h
 *
 *  Created on: 15/4/2017
 *      Author: utnso
 */

#ifndef QEPD_H_
#define QEPD_H_

#include "commons/log.h"
#include "commons/config.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define RUTA_CONFIG "config.cfg"
#define DIVIDE_ROUNDUP(x,y) ((x - 1) / y + 1)
#define PACKED __attribute__((packed, aligned(1)))

enum CodigoDeOperacion {

	//Handshake
	CONSOLA, MEMORIA, FILESYSTEM, CPU, KERNEL,

	//Todos
	MENSAJE, PETICION_CORRECTA,

	//Consola - Kernel
	INICIAR_PROGRAMA, PAGINAS_STACK, DESCONECTAR_CONSOLA,

	//Kernel - CPU
	PCB_INCOMPLETO, DESCONEXION_CPU, PCB_COMPLETO,
	PCB_EXCEPCION, QUANTUM, QUANTUM_SLEEP,
	OBTENER_VALOR_VARIABLE, ASIGNAR_VALOR_VARIABLE,
	WAIT, SIGNAL, BLOQUEAR, PCB_BLOQUEADO,
	ALGORITMO_ACTUAL,

	//Kernel - CPU - File system
	ABRIR_ARCHIVO, LEER_ARCHIVO, ESCRIBIR_ARCHIVO,
	CERRAR_ARCHIVO, BORRAR_ARCHIVO, CREAR_ARCHIVO,
	VALIDAR_ARCHIVO,

	//CPU - Memoria
	INSTRUCCION,

	//Kernel - Memoria - CPU
	EXCEPCION, SOLICITAR_BYTES, ALMACENAR_BYTES, FRAME_SIZE,

	//Kernel - Memoria
	INICIALIZAR_PROGRAMA, ASIGNAR_PAGINAS, FINALIZAR_PROGRAMA, LIBERAR_PAGINA

};

#define FINALIZO_CORRECTAMENTE 0
#define NO_SE_PUDIERON_RESERVAR_RECURSOS -1
#define ARCHIVO_NO_EXISTE -2
#define INTENTO_LEER_SIN_PERMISOS -3
#define INTENTO_ESCRIBIR_SIN_PERMISOS -4
#define EXCEPCION_MEMORIA -5
#define DESCONEXION_CONSOLA -6
#define COMANDO_FINALIZAR_PROGRAMA -7
#define INTENTO_RESERVAR_MAS_MEMORIA_QUE_PAGINA -8
#define NO_SE_PUEDEN_ASIGNAR_MAS_PAGINAS -9
#define EXCEPCION_KERNEL -10
#define VARIABLE_SIN_DECLARAR -11
#define ETIQUETA_INEXISTENTE -12
#define FALLO_DE_SEGMENTO -13
#define SOBRECARGA_STACK -14
#define SEMAFORO_INEXISTENTE -15
#define VARIABLE_COMPARTIDA_INEXISTENTE -16
#define REDEFINICION_VARIABLE -17
#define SIN_DEFINICION -20

typedef struct headerDeLosRipeados {
	unsigned short bytesDePayload;
	char codigoDeOperacion;
} PACKED headerDeLosRipeados;

extern t_log* logger;
extern t_config* config;

void 	conectar(int *,char *,int);
void 	configurar(char*);
void 	enviar_header(int, char, int);
int 	existe_archivo(const char *);
void 	handshake(int,char);
void 	logear_error(char*, int,...);
void 	logear_info(char*,...);
int 	recibir_header(int, headerDeLosRipeados *);

#endif /* QEPD_H_ */
