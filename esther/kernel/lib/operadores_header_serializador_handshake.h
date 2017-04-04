/*
 * operadores_header_serializador_handshake.h
 *
 *  Created on: 3/4/2017
 *      Author: utnso
 */

#ifndef OPERADORES_HEADER_SERIALIZADOR_HANDSHAKE_H_
#define OPERADORES_HEADER_SERIALIZADOR_HANDSHAKE_H_

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>

enum CodigoDeOperacion {
	MENSAJE, CONSOLA, MEMORIA, FILESYSTEM, CPU
};

struct headerDeLosRipeados {
	unsigned short bytesDePayload;
	char codigoDeOperacion; // 0 (mensaje). Handshake: 1 (consola), 2 (memoria), 3 (filesystem), 4 (cpu), 5 (kernel)
};

void serializarHeader(struct headerDeLosRipeados *header, char *buffer);

void deserializarHeader(struct headerDeLosRipeados *header, char *buffer);

void handshake(int socket, char operacion);

#endif /* OPERADORES_HEADER_SERIALIZADOR_HANDSHAKE_H_ */
