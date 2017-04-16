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

enum CodigoDeOperacion {
	MENSAJE, CONSOLA, MEMORIA, FILESYSTEM, CPU
};

typedef struct headerDeLosRipeados {
	unsigned short bytesDePayload;
	char codigoDeOperacion; // 0 (mensaje). Handshake: 1 (consola), 2 (memoria), 3 (filesystem), 4 (cpu), 5 (kernel)
}__attribute__((packed, aligned(1))) headerDeLosRipeados;

extern t_log* logger;
extern t_config* config;

extern int servidor; //kernel
extern char IP_KERNEL[16]; // 255.255.255.255 = 15 caracteres + 1 ('\0')
extern int PUERTO_KERNEL;

int existeArchivo(const char *);
void conectarAKernel();
void configurar(char*);
void handshake(int,char);
void serializarHeader(headerDeLosRipeados *, void *);
void deserializarHeader(headerDeLosRipeados *, void *);
void logearInfo(char*,...);
void logearError(char*, int,...);

#endif /* QEPD_H_ */
