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

enum CodigoDeOperacion {CONSOLA,MEMORIA,FILESYSTEM,CPU,KERNEL,MENSAJE,INICIAR_PROGRAMA,FINALIZAR_PROGRAMA,ERROR_MULTIPROGRAMACION};

typedef struct headerDeLosRipeados {
	unsigned short bytesDePayload;
	char codigoDeOperacion;
}__attribute__((packed, aligned(1))) headerDeLosRipeados;

extern t_log* logger;
extern t_config* config;

void 	conectar(int *,char *,int);
void 	configurar(char*);
void 	enviarHeader(int, char, int);
int 	existeArchivo(const char *);
void 	handshake(int,char);
void 	logearError(char*, int,...);
void 	logearInfo(char*,...);
int 	recibirHeader(int, headerDeLosRipeados *);
void 	serializarHeader(headerDeLosRipeados *, void *);

#endif /* QEPD_H_ */
