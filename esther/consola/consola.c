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

int servidor;

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

void deserializarHeader(struct headerDeLosRipeados *header, char *buffer) {
	short *cache = (short*) buffer;
	header->bytesDePayload = *cache;
	cache++;
	header->codigoDeOperacion = *cache;
}

/*
 * ↓ PARTE GRAFICA Y SUS FUNCIONES ↓
 */

void imprimirOpcionesDeConsola() {
	printf("--------------------\n");
	printf("\n");
	printf("BIEVENIDO A LA CONSOLA\n");
	printf("SUS OPCIONES:\n");
	printf("\n");
	printf("1. Iniciar programa AnSISOP\n");
	printf("2. Finalizar programa AnSISOP\n");
	printf("3. Desconectar consola\n");
	printf("4. Enviar mensaje\n");
	printf("5. Limpiar mensajes\n");
	printf("6. Mostrar opciones nuevamente\n");
	printf("\n");
	printf("--------------------\n");
}

void iniciarPrograma() {
	printf("TODO\n");
} // TODO

void desconectarPrograma() {
	printf("TODO\n");
} // TODO

void desconectarConsola() {
	printf("TODO\n");
} // TODO

void enviarMensaje() {
	char mensaje[512];
	scanf("%s", mensaje);
	struct headerDeLosRipeados headerDeMiMensaje;
	headerDeMiMensaje.bytesDePayload = sizeof(mensaje);
	headerDeMiMensaje.codigoDeOperacion = MENSAJE;

	char *headerComprimido;
	headerComprimido = malloc(sizeof(headerDeMiMensaje));
	serializarHeader(&headerDeMiMensaje, headerComprimido);

	send(servidor, headerComprimido, strlen(headerComprimido), 0); // Mando el header primero
	send(servidor, mensaje, strlen(mensaje), 0); // Mando el mensaje después

	printf("\nMensaje enviado, coloque otra opción. Opcion 6 para más información\n");
}

void limpiarPantalla() {
	printf("\033[H\033[J");
}

void interaccionConsola() {
	int opcion;
	imprimirOpcionesDeConsola();
	while(1) {
		scanf("%i", &opcion);

		switch(opcion) {
		case 1 : {
			iniciarPrograma(); // TODO
			break;
		}
		case 2 : {
			desconectarPrograma(); // TODO
			break;
		}
		case 3 : {
			desconectarConsola(); // TODO
			break;
		}
		case 4 : {
			enviarMensaje();
			break;
		}
		case 5 : {
			limpiarPantalla();
			break;
		}
		case 6 : {
			imprimirOpcionesDeConsola();
			break;
		}
		default : {
			printf("Coloque una opcion correcta (1, 2, 3, 4, 5 o 6)\n");
			break;
		}
		}
	}
}

/*
 * ↑ PARTE GRAFICA Y SUS FUNCIONES ↑
 */

int main(void) {

	struct sockaddr_in direccionServidor;
	direccionServidor.sin_family = AF_INET;
	direccionServidor.sin_addr.s_addr = inet_addr("127.0.0.1");
	direccionServidor.sin_port = htons(8081);

	servidor = socket(AF_INET, SOCK_STREAM, 0);

	if (connect(servidor, (struct sockaddr *) &direccionServidor,sizeof(direccionServidor))) {
		printf("No se pudo conectar al servidor\n");
		return 1;
	}

	handshake(servidor, 1);

/*

	int buffersize = 256;
	char buffer[buffersize];
	int i;
	for (i = 0; i < buffersize; i++) {
		buffer[i] = '\0';
	}

	while (1) {
		printf("\nEscribir mensaje: ");
		scanf("%s", buffer);

		if (send(servidor, buffer, strlen(buffer), 0) < 0) {
			printf("\nNo se pudo enviar el mensaje\n");
			return 1;
		}

		if (recv(servidor, buffer, buffersize, 0) < 0) {
			printf("\nSe terminó la conexión\n");
			return 1;
		}

		buffer[buffersize] = '\0';
		printf("\nMensaje del servidor: %s", buffer);
	}

*/

	interaccionConsola();

	close(servidor);
	return 0;
}
