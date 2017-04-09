/*
#include "operadores_header_serializador_handshake.h"

int main(void) {

	struct sockaddr_in direccionServidor;
	direccionServidor.sin_family = AF_INET;
	direccionServidor.sin_addr.s_addr = htonl(INADDR_ANY);
	direccionServidor.sin_port = htons(8081);

	char buf[256]; // Declarado temporalmente, luego se maneja con el protocolo

	int bytesRecibidos;

	int j;
	j = 0;

	int i;
	i = 0; //contador del main loop

	fd_set master;    // set maestro de fyle descriptors
	fd_set read_fds;  // temp file descriptor list for select()
	int fdmax;  // numero maximo hasta el que voy a buscar

	socklen_t addrlen;

	int newfd;  // direccion de ultimo cliente accepteado

	struct sockaddr_storage remoteaddr; // estructura de la direccion del cliente

	int servidor = socket(AF_INET, SOCK_STREAM, 0);

	int activado = 1;
	setsockopt(servidor, SOL_SOCKET, SO_REUSEADDR, &activado, sizeof(activado));

	if (bind(servidor, (void*) &direccionServidor, sizeof(direccionServidor))
			!= 0) {
		perror("Falló el bind");
		return 1;
	}

	if (listen(servidor, 100) == -1) {
		perror("listen");
		exit(3);
	}

	printf("Estoy escuchando\n");

	FD_SET(servidor, &master);

	fdmax = servidor;  //este es el maximo por ser el primero (el servidor)

	// main loop
	for (;;) {
		read_fds = master; // copy it
		if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
			perror("select");
			exit(4);
		}

		// Empiezo a recorrer los sockets para encontrar data read
		for (i = 0; i <= fdmax; i++) {
			if (FD_ISSET(i, &read_fds)) { // Si encontre alguno que este en read...
				if (i == servidor) { //Si encontre el listener tengo una nueva conexion
					addrlen = sizeof remoteaddr;
					newfd = accept(servidor, (struct sockaddr *) &remoteaddr,
							&addrlen);

					if (newfd == -1) {
						perror("error en el accept");
					} else {
						FD_SET(newfd, &master); // Agrego el nuevo socket
						if (newfd > fdmax) {    // Chequeo el maximo
							fdmax = newfd;

							printf("Nueva conexion en el socket %d\n", newfd);

						}
					}
				}
				else { // Si no es una nueva conexion leo datos del cliente

					if ((bytesRecibidos = recv(i, buf, sizeof buf, 0))
							<= 0) {

						if (bytesRecibidos == 0) { // error o se desconecta el conector

							printf("socket %d hung up\n", i);
						} else {
							perror("error en el recv");
						}

						close(i); // cierro la conexion

						FD_CLR(i, &master); // lo saco del set master
					} else {   // El cliente no se desconecto, hay datos

						for (j = 0; j <= fdmax; j++) {
							// Le voy a mandar esto a todos
							if (FD_ISSET(j, &master)) {

								if (j != servidor && j != i) { // no quiero mandarmelo a mi ni al servidor
									if (send(j, buf, bytesRecibidos, 0)
											== -1) {
										perror("send");
									}
								}
							}
						}
					}
				}
			}
		}
	}
	return 0;
}
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "commons/log.h"
#include "commons/config.h"

#define MOSTRAR_LOGS_EN_PANTALLA true

#define RUTA_CONFIG "config.cfg"
#define RUTA_LOG "consola.log"

t_log* logger;
t_config* config;

#define PORT "8081"   // port we're listening on

#define MAX_NUM_CLIENTES 100

enum CodigoDeOperacion {
    MENSAJE, CONSOLA, MEMORIA, FILESYSTEM, CPU
};

struct headerDeLosRipeados {
    unsigned short bytesDePayload;
    char codigoDeOperacion; // 0 (mensaje). Handshake: 1 (consola), 2 (memoria), 3 (filesystem), 4 (cpu), 5 (kernel)
};

struct miCliente {
    short socketCliente;
    char identificador;
};

void limpiarClientes(struct miCliente *clientes);
void analizarCodigosDeOperacion(int socketCliente, char codigoDeOperacion, struct miCliente *clientes);
void analizarHeader(int socketCliente, char* buffer, struct miCliente *clientes);
void leerMensaje(int socketCliente, short bytesDePayload);
void corroborarFuturoCliente(int newfd);
void cerrarConexion(int socketCliente, char* motivo);
int posicionSocket(int socketCliente, struct miCliente *clientes);

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(void) {
    struct miCliente misClientes[MAX_NUM_CLIENTES];

    limpiarClientes(&misClientes);

    fd_set master;    // master file descriptor list
    fd_set read_fds;  // temp file descriptor list for select()
    int fdmax;        // maximum file descriptor number

    int listener;     // listening socket descriptor
    int newfd;        // newly accept()ed socket descriptor
    struct sockaddr_storage remoteaddr; // client address
    socklen_t addrlen;

    char buf[256];    // buffer for client data
    int nbytes;

    char remoteIP[INET6_ADDRSTRLEN];

    int i, j, rv;

    struct addrinfo hints, *ai, *p;

    FD_ZERO(&master);    // clear the master and temp sets
    FD_ZERO(&read_fds);

    // get us a socket and bind it
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
        fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
        exit(1);
    }

    for(p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) {
            continue;
        }

        // lose the pesky "address already in use" error message
        int activado = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &activado, sizeof(activado));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }

        break;
    }

    // if we got here, it means we didn't get bound
    if (p == NULL) {
        fprintf(stderr, "selectserver: failed to bind\n");
        exit(2);
    }

    freeaddrinfo(ai); // all done with this

    // listen
    if (listen(listener, 10) == -1) {
        perror("listen");
        exit(3);
    }
    printf("Estoy escuchando\n");
    // add the listener to the master set
    FD_SET(listener, &master);

    // keep track of the biggest file descriptor
    fdmax = listener; // so far, it's this one

    // main loop
    for(;;) {
        read_fds = master; // copy it
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            exit(4);
        }

        // run through the existing connections looking for data to read
        for(i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) { // we got one!!
                if (i == listener) {
                    // handle new connections
                    addrlen = sizeof remoteaddr;
                    newfd = accept(listener,
                        (struct sockaddr *)&remoteaddr,
                        &addrlen);

                    if (newfd == -1) {
                        perror("accept");
                    } else {
                        FD_SET(newfd, &master); // add to master set
                        if (newfd > fdmax) {    // keep track of the max
                            fdmax = newfd;
                        }
                        printf("selectserver: new connection from %s on "
                            "socket %d\n",
                            inet_ntop(remoteaddr.ss_family,
                                get_in_addr((struct sockaddr*)&remoteaddr),
                                remoteIP, INET6_ADDRSTRLEN),
                            newfd);
//                        corroborarFuturoCliente(newfd); // TODO
                    }
                } else {
                    // handle data from a client
                    if ((nbytes = recv(i, buf, sizeof buf, 0)) <= 0) {
                        // got error or connection closed by client
                        if (nbytes == 0) {
                            // connection closed
                            printf("selectserver: socket %d hung up\n", i);
                        } else {
                            perror("recv");
                        }
                        cerrarConexion(i, "time-out");
                        FD_CLR(i, &master); // remove from master set
                    } else {
                    	char *respuesta = "Header recibido";
                    	send(i, respuesta, strlen(respuesta) + 1, 0);
                        // we got some data from a client
                        analizarHeader(i, &buf, &misClientes);
                        /*
                        buf[nbytes]='\0';
                        printf("%s", buf);
                        for(j = 0; j <= fdmax; j++) {
                            // send to everyone!
                            if (FD_ISSET(j, &master)) {
                                // except the listener and ourselves
                                if (j != listener && j != i) {
                                    if (send(j, buf, nbytes, 0) == -1) {
                                        perror("send");
                                    }
                                }
                            }
                        }*/
                    }
                } // END handle data from client
            } // END got new incoming connection
        } // END looping through file descriptors
    } // END for(;;)--and you thought it would never end!

    return 0;
}

void deserializarHeader(struct headerDeLosRipeados *header, char *buffer) {
    short *cache = (short*) buffer;
    header->bytesDePayload = *cache;
    cache++;
    header->codigoDeOperacion = *cache;
}

/**
 * Analiza el contenido del header, y respecto a ello realiza distintas acciones
 */
void analizarHeader(int socketCliente, char* buffer, struct miCliente *clientes) {
    struct headerDeLosRipeados header;
    deserializarHeader(&header, buffer);
    if(header.codigoDeOperacion == MENSAJE) {
        if(header.bytesDePayload <= 0) {
            printf("El cliente %i intento mandar un mensaje sin contenido", socketCliente);
        } else {
            leerMensaje(socketCliente, header.bytesDePayload);
        }
    } else {
        analizarCodigosDeOperacion(socketCliente, header.codigoDeOperacion, &clientes); // TODO
    }

}

void leerMensaje(int socketCliente, short bytesDePayload) {
    char mensaje[512];
    if(bytesDePayload <= 0){
        close(socketCliente);
        log_error(logger,"Cliente desconectado luego de intentar leer mensaje");
        exit(0);
    }
    mensaje[bytesDePayload]='\0';
    printf("\nMensaje recibido: %s\n",mensaje);
}

void limpiarClientes(struct miCliente *clientes) {
    int i;
    for(i = 0; i < MAX_NUM_CLIENTES; i++) {
        clientes[i].socketCliente = -1;
        clientes[i].identificador = 255;
    }
}

int posicionSocket(int socketCliente, struct miCliente *clientes) {
    int i;
    for(i = 0; i < MAX_NUM_CLIENTES; i++) {
        if(clientes[i].identificador == socketCliente) {
            return i;
        }
    }

    printf("Error de no existencia de socket que creiamos que existia\n");
    exit(EXIT_FAILURE);
}

void analizarCodigosDeOperacion(int socketCliente, char codigoDeOperacion, struct miCliente *clientes) {
    char codigoDelCliente = clientes[posicionSocket(socketCliente, clientes)].identificador;
    switch(codigoDelCliente) {
        case CONSOLA:
            // TODO
            break;
        case MEMORIA:
            // TODO
            break;
        case FILESYSTEM:
            // TODO
            break;
        case CPU:
            // TODO
            break;
        default:
            printf("TODO\n");
            // TODO
    }
}


/*
void corroborarFuturoCliente(int socketCliente){

	recv(i, buf, sizeof buf, 0)

}
*/
void cerrarConexion(int socketCliente, char* motivo){
	close(socketCliente);
	printf("El socket %i ha sido desconectado por %s", socketCliente, motivo);
}


