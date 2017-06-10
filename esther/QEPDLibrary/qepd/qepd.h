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
	CONSOLA, MEMORIA, FILESYSTEM, CPU, KERNEL, // Handshake
	EXCEPCION_DE_SOLICITUD, PCB_INCOMPLETO, INICIAR_PROGRAMA, FRAME_SIZE,// Mensajes provenientes de procesos varios
	MENSAJE, ABRIR_ARCHIVO, LEER_ARCHIVO, ESCRIBIR_ARCHIVO, CERRAR_ARCHIVO, PEDIR_MEMORIA_VARIABLE_OK, QUANTUM, ASIGNAR_VALOR_VARIABLE_OK, RETORNAR_VARIABLE_OK, PEDIR_MEMORIA_OK, WAIT_SIN_BLOQUEO, WAIT_CON_BLOQUEO, SIGNAL_OK, // Mensajes provenientes de Kernel
	PCB_COMPLETO, PCB_EXCEPCION, OBTENER_VALOR_VARIABLE, PEDIR_MEMORIA_VARIABLE, ASIGNAR_VALOR_VARIABLE, INSTRUCCION, RETORNAR_VALOR, PEDIR_MEMORIA, WAIT, SIGNAL, // Mensajes provenientes de la CPU
	INSTRUCCION_OK, OBTENER_VALOR_VARIABLE_OK, // Mensajes provenientes de la memoria
	EXCEPCION,

	//Interfaz Memoria:
	INICIALIZAR_PROGRAMA, SOLICITAR_BYTES, ALMACENAR_BYTES,
	ASIGNAR_PAGINAS, FINALIZAR_PROGRAMA, LIBERAR_PAGINA
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
#define SEGMENTATION_FAULT -13
#define SEMAFORO_INEXISTENTE -14
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
