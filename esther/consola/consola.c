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
	/*unsigned */short bytesDePayload;
	char codigoDeOperacion; // 0 (mensaje). Handshake: 1 (consola), 2 (memoria), 3 (filesystem), 4 (cpu), 5 (kernel)
};

void serializarHeader(struct headerDeLosRipeados header, char **buffer) {
	//buffer = malloc(sizeof(struct headerDeLosRipeados)+1);
	sprintf(*buffer,"%u %i", header.codigoDeOperacion, header.bytesDePayload);
	printf("IMPRESION DE HANDY SERIALIZADO: %s\n", *buffer);
}

void handshake(int servidor, char operacion) {
	printf("Conectando a servidor 0 porciento\n");
/*	printf("Conectando a servidor 23 porciento\n");
	sleep(1);
	printf("Conectando a servidor 55 porciento\n");
	printf("Conectando a servidor 84 porciento\n");
	sleep(2);*/
	struct headerDeLosRipeados handy;
	handy.bytesDePayload = 0;
	handy.codigoDeOperacion = operacion;

	char *buffer;
	int buffersize = sizeof(struct headerDeLosRipeados)+1;
	printf("%i\n",buffersize);
	buffer = malloc(buffersize);

	serializarHeader(handy,&buffer);
	buffer[buffersize]='\0';
	printf("%s\n",buffer);
	send(servidor, (void*)"asdf", buffersize, 0);

	/*int mensaje = recv(servidor, (void *) respuesta, sizeof(respuesta), 0);




	if(mensaje > 0) {
		printf("Conectado a servidor 100 porciento!!!\n");
	} else {
		printf("Ripeaste\n");
		exit(0);
	};*/
	free(buffer);
}



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
	direccionServidor.sin_addr.s_addr = inet_addr("127.0.0.1");
	direccionServidor.sin_port = htons(8081);

	int cliente = socket(AF_INET, SOCK_STREAM, 0);

	int servidor = connect(cliente, (struct sockaddr *) &direccionServidor, sizeof(direccionServidor));

	if (servidor != 0) {
		printf("No se pudo conectar al servidor\n");
		return 1;

	}
	int buffersize = 256;
	char buffer[buffersize];
	int i;
	for (i=0;i<buffersize;i++){
		buffer[i]='\0';
	}
	while(1){
		printf("Escribir mensaje: ");
		scanf("%s",&buffer);

		if(send(servidor,&buffer,strlen(buffer),0) < 0){
			printf("\nNo se pudo enviar el mensaje\n");
			return 1;
		}

		if(recv(servidor,buffer,buffersize,0) < 0)
		{
			printf("Se terminó la conexión\n");
			return 1;
		}

		buffer[buffersize]='\0';
		printf("Mensaje del servidor: %s\n",buffer);
	}

	close(servidor);
	return 0;
}
