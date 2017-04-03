/*
 * consola.c
 *
 *  Created on: 1/4/2017
 *      Author: utnso
 */

#include "operadores_header_serializador_handshake.h"

int servidor;

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
	close(servidor);
	printf("Hasta la vuelta!\n");
	exit(EXIT_SUCCESS);
}

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

	return 0;
}
