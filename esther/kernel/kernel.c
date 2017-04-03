/*
 * kernel.c
 *
 *  Created on: 2/4/2017
 *      Author: utnso
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>

struct headerDeLosRipeados {
	unsigned short bytesDePayload;
	char codigoDeOperacion; // 0 (mensaje). Handshake: 1 (consola), 2 (memoria), 3 (filesystem), 4 (cpu), 5 (kernel)
};

void deserializarHeader(struct headerDeLosRipeados *header, char *buffer) {
	/*int codigoDeOperacion;
	int bytesDePayload;
	sscanf(buffer, "%i %i",&codigoDeOperacion, &bytesDePayload);
	header -> codigoDeOperacion = codigoDeOperacion;
	header -> bytesDePayload = bytesDePayload;*/
	short *cache = (short*) buffer;
	header->bytesDePayload = *cache;
	cache++;
	header->codigoDeOperacion = *cache;
}

int main(void) {
	struct sockaddr_in direccionServidor;
	direccionServidor.sin_family = AF_INET;
	direccionServidor.sin_addr.s_addr = htonl(INADDR_ANY);
	direccionServidor.sin_port = htons(8081);

	struct sockaddr_in direccionCliente;

	socklen_t len;


	int servidor = socket(AF_INET, SOCK_STREAM, 0);

	int activado = 1;
	setsockopt(servidor, SOL_SOCKET, SO_REUSEADDR, &activado, sizeof(activado));


	if (bind(servidor, (void*) &direccionServidor, sizeof(direccionServidor)) != 0) {
		perror("Falló el bind");
		return 1;
	}


	if (listen(servidor, 100) != 0) {
		printf("Falló la escucha\n");
		return 1;
	}
	printf("Estoy escuchando\n");
	len = sizeof direccionCliente;
	int cliente = accept(servidor,(struct sockaddr *)&direccionCliente,&len);
	if (cliente < 0) {
		printf("Falló la conexión a cliente\n");
		return 1;
	}
	printf("Cliente conectado\n");

	// Recibir un handshake del cliente conectado

	int bufferSize = sizeof(direccionServidor);
	char *buffer = malloc(bufferSize);
	int bytesRecibidos;
	bytesRecibidos = recv(cliente, buffer, bufferSize, 0);
	struct headerDeLosRipeados handyRecibido;
	deserializarHeader(&handyRecibido, buffer);
	free(buffer);
	printf("La cantidad de bytes de Payload es: %u\n", handyRecibido.bytesDePayload);
	printf("El codigo de operacion es: %d\n", handyRecibido.codigoDeOperacion);

	// responde al cliente luego de recibir un handshake
	char *respuesta = "todo piola wachin";
	if (send(cliente, respuesta, strlen(respuesta), 0) < 0) {
		printf("No se pudo retransmitir el mensaje\n");
		return 1;
	}

	/* todo esto funciona, lo comento para testear handshake()
	int buffersize = 256;
	char buffer[buffersize];
	int bytesRecibidos;
	int i;
	for (i=0; i < buffersize; i++) {
		buffer[i]='\0';
	}
	while(1) {
		bytesRecibidos = recv(cliente,buffer,buffersize, 0);
		buffer[bytesRecibidos]='\0';
	    if(bytesRecibidos <= 0){
	    	printf("Cliente desconectado\n");
	        return 1;
	    }
		printf("Mensaje recibido: %s\n", buffer);
		if (send(cliente,buffer,strlen(buffer),0) < 0){
			printf("No se pudo retransmitir el mensaje\n");
			return 1;
		}
		printf("Mensaje retransmitido\n");
	}*/
}


