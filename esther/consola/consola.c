/*
 * consola.c
 *
 *  Created on: 1/4/2017
 *      Author: utnso
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>

enum {
	MENSAJE, CONSOLA, MEMORIA, FILESYSTEM, CPU
};

struct headerDeLosRipeados {
	unsigned short bytesDePayload;
	char codigoDeOperacion; // 0 (mensaje). Handshake: 1 (consola), 2 (memoria), 3 (filesystem), 4 (cpu), 5 (kernel)
};

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

void handshake(int socket, char operacion) {
	printf("Conectando a servidor 0\%\n");
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
	char respuesta[6000];
	int bytesRecibidos = recv(socket, (void *) &respuesta, sizeof(respuesta), 0);

	if (bytesRecibidos > 0) {
		printf("Conectado a servidor 100\%\n");
		printf("Mensaje del servidor: \"%s\"\n", respuesta);
	}
	else {
		printf("Ripeaste\n");
		exit(0);
	}
	free(buffer);
}

void deserializarHeader(struct headerDeLosRipeados *header, char *buffer) {
	short *cache = (short*) buffer;
	header->bytesDePayload = *cache;
	cache++;
	header->codigoDeOperacion = *cache;
}

int main(void) {
	int socket_fd;

	struct sockaddr_in direccionServidor;
	direccionServidor.sin_family = AF_INET;
	direccionServidor.sin_addr.s_addr = inet_addr("127.0.0.1");
	direccionServidor.sin_port = htons(8081);

	socket_fd = socket(AF_INET, SOCK_STREAM, 0);

	if (connect(socket_fd, (struct sockaddr *) &direccionServidor,
			sizeof(direccionServidor))) {
		printf("No se pudo conectar al servidor\n");
		return 1;
	}

	handshake(socket_fd, 1);

	/* todo esto funciona, lo comento para testear handshake()
	int buffersize = 256;
	char buffer[buffersize];
	int i;
	for (i = 0; i < buffersize; i++) {
		buffer[i] = '\0';
	}

	while (1) {
		printf("Escribir mensaje: ");
		scanf("%s", buffer);
		printf("\n %s", buffer);

		if (send(socket_fd, buffer, strlen(buffer), 0) < 0) {
			printf("\nNo se pudo enviar el mensaje\n");
			return 1;
		}

		if (recv(socket_fd, buffer, buffersize, 0) < 0) {
			printf("Se terminó la conexión\n");
			return 1;
		}

		buffer[buffersize] = '\0';
		printf("Mensaje del servidor: %s\n", buffer);
	}*/

	close(socket_fd);
	return 0;
}
