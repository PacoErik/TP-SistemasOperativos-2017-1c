#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netdb.h>
#include <string.h>

int main(void) {
	struct addrinfo *direcciones;

	struct addrinfo hints;
	memset(&hints, 0, sizeof hints);

	hints.ai_family = AF_INET; // IPv4
	hints.ai_socktype = SOCK_STREAM; // TCP
	hints.ai_flags = AI_PASSIVE;

	int rv = getaddrinfo(NULL, "8082", &hints, &direcciones);
	if (rv != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		exit(EXIT_FAILURE);
	}

	int servidor; // socket de escucha

	struct addrinfo *p; // Puntero para recorrer la lista de direcciones

	// Recorrer la lista hasta encontrar una direccion disponible para el bind
	for (p = direcciones; p != NULL; p = p->ai_next) {

		servidor = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (servidor == -1) {	// Devuelve 0 si hubo error
			continue;
		}

		int activado = 1;
		setsockopt(servidor, SOL_SOCKET, SO_REUSEADDR, &activado,
				sizeof(activado));

		if (bind(servidor, p->ai_addr, p->ai_addrlen) == 0) {
			break; // Se encontro una direccion disponible
		}

		close(servidor);
	}

	if (p == NULL) {
		fprintf(stderr, "Could not bind\n");
		exit(EXIT_FAILURE);
	}

	freeaddrinfo(direcciones); // No necesito mas la lista de direcciones

	if (listen(servidor, 10) == -1) {
		fprintf(stderr, "Could not listen\n");
		exit(EXIT_FAILURE);
	}

	printf("Listening ♩♪♫♬\n");

	fd_set conectados; // Set de FDs conectados
	fd_set read_fds; // sockets de lectura

	FD_ZERO(&conectados);
	FD_ZERO(&read_fds);

	FD_SET(servidor, &conectados);
	FD_SET(fileno(stdin), &conectados);

	int fdmax;	// valor maximo de los FDs
	fdmax = servidor; // Por ahora hay un solo socket, por eso es el maximo

	for (;;) {
		read_fds = conectados;
		if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
			fprintf(stderr, "Error en el select\n");
		}

		// Se detecta algun cambio en alguno de los sockets

		struct sockaddr_in direccionCliente;

		int i;
		for (i = 0; i <= fdmax; i++) {
			if (FD_ISSET(i, &read_fds) == 0) { // No hubo cambios en este socket
				continue;
			}
			// Se detecta un cambio en el socket de escucha => hay un nuevo cliente
			if (i == servidor) {
				socklen_t addrlen = sizeof direccionCliente;
				int nuevoCliente; // Socket del nuevo cliente conectado
				nuevoCliente = accept(servidor,
						(struct sockaddr *) &direccionCliente, &addrlen);

				if (nuevoCliente == -1) {
					fprintf(stderr, "Fallo en el accept\n");
				} else {
					FD_SET(nuevoCliente, &conectados); // Agregarlo al set
					if (nuevoCliente > fdmax) {
						fdmax = nuevoCliente; // Cambia el maximo
					}
					printf("Nueva conexión en el socket %d\n", nuevoCliente);
				}
			}
			// Un cliente mando un mensaje
			else {
				int msglen = strlen("Thread XX") + 1;
				char *msg = malloc(msglen);
				if (recv(i, (void *) msg, msglen, 0)) {
					printf("Mensaje del socket %d: %s\n", i, msg);
				} else {
					printf("El socket %d se desconecto\n", i);
					FD_CLR(i, &conectados);
				}
				free(msg);
			}
		}
	}
}
