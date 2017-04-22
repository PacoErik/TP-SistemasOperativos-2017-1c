#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>

#define MAX_THREADS 10

static void* conectar(void*);

int main(void) {
	pthread_t hilo[MAX_THREADS];
	int i;
	for (i = 0; i < MAX_THREADS; i++) {
		// crea un hilo que se conectara al servidor
		int *a = malloc(sizeof(int)); // si mando i directamente no funca??
		*a = i;
		pthread_create(&hilo[i], NULL, &conectar, a);
	}
	for (i = 0; i < MAX_THREADS; i++) {
		pthread_join(hilo[i], NULL);
	}
	return EXIT_SUCCESS;
}

static void* conectar(void* b) {
	int* hiloID = b;
	struct sockaddr_in direccionServidor;
	direccionServidor.sin_family = AF_INET;
	direccionServidor.sin_addr.s_addr = inet_addr("127.0.0.1");
	direccionServidor.sin_port = htons(8082);
	int servidor = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(servidor, (struct sockaddr *) &direccionServidor,
			sizeof(direccionServidor)) < 0) {
		printf("Thread %i: No se pudo conectar al servidor\n", *hiloID);
		close(servidor);
	}
	int msglen = strlen("Thread XX") + 1;
	char msg[msglen];
	sprintf(msg, "Thread %02i", *hiloID);
	send(servidor, msg, msglen, 0);
	return NULL;
}
