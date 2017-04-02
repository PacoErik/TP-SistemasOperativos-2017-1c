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

enum {MENSAJE, CONSOLA, MEMORIA, FILESYSTEM, CPU};

struct headerDeLosRipeados {
	unsigned short bytesDePayload;
	char codigoDeOperacion; // 0 (mensaje). Handshake: 1 (consola), 2 (memoria), 3 (filesystem), 4 (cpu), 5 (kernel)
};

void handshake(int servidor, char operacion) {
	printf("Conectando a servidor 0 porciento\n");
	printf("Conectando a servidor 23 porciento\n");
	sleep(1);
	printf("Conectando a servidor 55 porciento\n");
	printf("Conectando a servidor 84 porciento\n");
	sleep(2);
	struct headerDeLosRipeados handy;
	handy.bytesDePayload = 0;
	handy.codigoDeOperacion = operacion;

	char *buffer;
	buffer = malloc(sizeof(struct headerDeLosRipeados));
	sprintf(buffer,"%c%i",handy.codigoDeOperacion,handy.bytesDePayload);

	send(servidor, (void*)buffer, sizeof(buffer), 0);

	/*int mensaje = recv(servidor, (void *) respuesta, sizeof(respuesta), 0);




	if(mensaje > 0) {
		printf("Conectado a servidor 100 porciento!!!\n");
	} else {
		printf("Ripeaste\n");
		exit(0);
	};*/
	free(buffer);
}

void serializarHeader(struct headerDeLosRipeados header, char **buffer) {
	//buffer = malloc(sizeof(struct headerDeLosRipeados)+1);
	sprintf(*buffer,"%u %i", header.codigoDeOperacion, header.bytesDePayload);
	printf("IMPRESION DE HANDY SERIALIZADO: %s\n", *buffer);
}

void deserializarHeader(struct headerDeLosRipeados *header, char *buffer) {
	int codigoDeOperacion;
	int bytesDePayload;
	sscanf(buffer, "%i %i",&codigoDeOperacion, &bytesDePayload);
	header -> codigoDeOperacion = codigoDeOperacion;
	header -> bytesDePayload = bytesDePayload;
}

int main(void) {
/*
	struct sockaddr_in direccionServidor;
	direccionServidor.sin_family = AF_INET;
	direccionServidor.sin_addr.s_addr = inet_addr("127.0.0.1");
	direccionServidor.sin_port = htons(8080);
	int cliente = socket(AF_INET, SOCK_STREAM, 0);
	int servidor = connect(cliente, (void*) &direccionServidor, sizeof(direccionServidor));

	if (servidor != 0) {
		perror("No se pudo conectar");
		return 1;
	}
	handshake(servidor, CONSOLA);*/
	struct headerDeLosRipeados handy;
	handy.bytesDePayload = 17;
	handy.codigoDeOperacion = 20;

	char *buffer;
	buffer = malloc(sizeof(struct headerDeLosRipeados)+1);
	serializarHeader(handy, &buffer);
	printf("IMPRESION DE HANDY SERIALIZADO: %s\n",buffer);

	struct headerDeLosRipeados estenoeshandy;
	deserializarHeader(&estenoeshandy, buffer);

	printf("IMPRESION DE HANDY DESERIALIZADO: %i %i\n",estenoeshandy.codigoDeOperacion,estenoeshandy.bytesDePayload);

	/*
	while (1) {
		char mensaje[1000];
		//char *mensaje;
		//mensaje=malloc(1000);
		scanf("%s", mensaje);

		send(cliente, mensaje, strlen(mensaje), 0);
	}*/
	return 0;
}
