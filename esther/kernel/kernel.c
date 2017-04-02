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
	int codigoDeOperacion;
	int bytesDePayload;
	sscanf(buffer, "%i %i",&codigoDeOperacion, &bytesDePayload);
	header -> codigoDeOperacion = codigoDeOperacion;
	header -> bytesDePayload = bytesDePayload;
}

int main(void) {
	struct sockaddr_in direccionServidor;
	direccionServidor.sin_family = AF_INET;
	direccionServidor.sin_addr.s_addr = INADDR_ANY;
	direccionServidor.sin_port = htons(8081);

	struct sockaddr_in direccionCliente;

	int len;


	int servidor = socket(AF_INET, SOCK_STREAM, 0);

	int activado = 1;
	setsockopt(servidor, SOL_SOCKET, SO_REUSEADDR, &activado, sizeof(activado));


	if (bind(servidor, (void*) &direccionServidor, sizeof(direccionServidor)) != 0) {
		perror("Falló el bind");
		return 1;
	}

	printf("Estoy escuchando\n");
	listen(servidor, 100);
	/*char *buffer;
	buffer=malloc(4);*/
	char buffer[3];
	int cliente = accept(servidor, &direccionCliente, &len);

	/*int bytesRecibidos = */recv(cliente, buffer, sizeof(buffer), 0);
	struct headerDeLosRipeados estenoeshandy;
	deserializarHeader(&estenoeshandy, buffer);

	printf("el valor del buffer es %s\n", buffer);
	printf("el valor de estenoeshandy %i, %i", estenoeshandy.bytesDePayload, estenoeshandy.codigoDeOperacion);

}


