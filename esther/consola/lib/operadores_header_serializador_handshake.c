/*
 * operadores_header_serializador_handshake.c
 *
 *  Created on: 4/4/2017
 *      Author: utnso
 */

#include "operadores_header_serializador_handshake.h"

void serializarHeader(struct headerDeLosRipeados *header, char *buffer) {
	/*//buffer = malloc(sizeof(struct headerDeLosRipeados)+1);
	 sprintf(*buffer,"%u %i", header.codigoDeOperacion, header.bytesDePayload);
	 printf("IMPRESION DE HANDY SERIALIZADO: %s\n", *buffer);
	 */
	short *cache = (short*) buffer;
	*cache = header->bytesDePayload;
	cache++;
	*cache = header->codigoDeOperacion;
}

void deserializarHeader(struct headerDeLosRipeados *header, char *buffer) {
	short *cache = (short*) buffer;
	header->bytesDePayload = *cache;
	cache++;
	header->codigoDeOperacion = *cache;
}

void handshake(int socket, char operacion) {
	printf("Conectando a servidor 0 porciento\n");
	/*	printf("Conectando a servidor 0 porciento\n");
	 printf("Conectando a servidor 23 porciento\n");
	 sleep(1);
	 printf("Conectando a servidor 55 porciento\n");
	 printf("Conectando a servidor 84 porciento\n");
	 sleep(2);*/
	struct headerDeLosRipeados handy;
	handy.bytesDePayload = 0;
	handy.codigoDeOperacion = operacion;

	int buffersize = sizeof(struct headerDeLosRipeados);
	// int buffersize = sizeof(struct headerDeLosRipeados) + 1;
	char *buffer = malloc(buffersize);
	//printf("%i\n", buffersize);
	//buffer = malloc(buffersize);

	//serializarHeader(handy,&buffer);
	//buffer[buffersize] = '\0';
	serializarHeader(&handy, buffer);
	//printf("%s\n", buffer);
	send(socket, (void*) buffer, buffersize, 0);

	// Recibe lo que responde el servidor
	char respuesta[1024];
	int bytesRecibidos = recv(socket, (void *) &respuesta, sizeof(respuesta), 0);

	if (bytesRecibidos > 0) {
		printf("Conectado a servidor 100 porciento\n");
		printf("Mensaje del servidor: \"%s\"\n", respuesta);
	}
	else {
		printf("Ripeaste\n");
		exit(0);
	}
	free(buffer);
}

