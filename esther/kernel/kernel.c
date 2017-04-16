#include <qepd/qepd.h>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

t_log* logger;
t_config* config;

char PUERTO_KERNEL[6];

#define MAX_NUM_CLIENTES 100

#define ID_CLIENTE(x) ID_CLIENTES[x-1]
static const char *ID_CLIENTES[] = {
	"Consola", "Memoria", "File System", "CPU"
};

typedef struct miCliente {
    short socketCliente;
    char identificador;
} miCliente;

void limpiarClientes(miCliente *);
void establecerConfiguracion();
void cerrarConexion(int socketCliente, char* motivo, miCliente *clientes);
int posicionSocket(int , miCliente *);
void agregarCliente(char , int , miCliente *);
void borrarCliente(int , miCliente *);
int recibirMensaje(int socketCliente, miCliente *clientes);
int recibirHandshake(int socketCliente, miCliente *clientes);
int recibirHeader(int socketCliente, miCliente *clientes);
int recibirPayload(int socketCliente, int bytesDePayload, miCliente *clientes);
int existeCliente(int socketCliente, miCliente *clientes);

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(void) {
	configurar("kernel");

	miCliente misClientes[MAX_NUM_CLIENTES];
    limpiarClientes(misClientes);

    int buffersize = sizeof(headerDeLosRipeados);
    void* buffer = malloc(buffersize);

    struct addrinfo hints; // Le da una idea al getaddrinfo() el tipo de info que debe retornar
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = AI_PASSIVE;


    /* getaddrinfo() retorna una lista de posibles direcciones para el bind */

	struct addrinfo *direcciones; // lista de posibles direcciones para el bind
    int rv = getaddrinfo(NULL, PUERTO_KERNEL, &hints, &direcciones); // si devuelve 0 hay un error
    if (rv != 0) {
    	// gai_strerror() devuelve el mensaje de error segun el codigo de error
        logearError("No se pudo abrir el server\n",true);
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
        setsockopt(servidor, SOL_SOCKET, SO_REUSEADDR, &activado, sizeof(activado));

        if (bind(servidor, p->ai_addr, p->ai_addrlen) == 0) {
            break; // Se encontro una direccion disponible
        }

        close(servidor);
    }

    if (p == NULL) {
        logearError("Fallo al bindear el puerto\n",true);
    }

    freeaddrinfo(direcciones); // No necesito mas la lista de direcciones

    if (listen(servidor, 10) == -1) {
        logearError("Fallo al escuchar\n",true);
    }
    logearInfo("Estoy escuchando\n");

    fd_set conectados; // Set de FDs conectados
    fd_set read_fds; // sockets de lectura

    FD_ZERO(&conectados);
    FD_ZERO(&read_fds);

    FD_SET(servidor, &conectados);

    int fdmax;	// valor maximo de los FDs
    fdmax = servidor; // Por ahora hay un solo socket, por eso es el maximo


    for(;;) {
        read_fds = conectados;
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            logearError("Error en el select\n",true);
        }

        // Se detecta algun cambio en alguno de los sockets

        struct sockaddr_in direccionCliente;

        int i;
        for(i = 0; i <= fdmax; i++) {
        	if (FD_ISSET(i, &read_fds) == 0) { // No hubo cambios en este socket
        		continue;
        	}
        	// Se detecta un cambio en el socket de escucha => hay un nuevo cliente
			if (i == servidor) {
		        socklen_t addrlen = sizeof direccionCliente;
		        int nuevoCliente; // Socket del nuevo cliente conectado
				nuevoCliente = accept(servidor, (struct sockaddr *)&direccionCliente, &addrlen);

				if (nuevoCliente == -1) {
					logearError("Fallo en el accept\n",false);
				}
				else {
					FD_SET(nuevoCliente, &conectados); // Agregarlo al set
					if (nuevoCliente > fdmax) {
						fdmax = nuevoCliente; // Cambia el maximo
					}
				    char direccionIP[INET_ADDRSTRLEN]; // string que contiene la direccion IP del cliente
					inet_ntop(AF_INET,	get_in_addr((struct sockaddr*) &direccionCliente), direccionIP, INET_ADDRSTRLEN);
					logearInfo("Nueva conexión desde %s en el socket %d\n", direccionIP, nuevoCliente);
				}
			}
			// Un cliente mando un mensaje
			else {
				if (existeCliente(i, misClientes) == 0) { // Nuevo cliente, debe enviar un handshake
					if (recibirHandshake(i, misClientes) == 0) {
						cerrarConexion(i, "El socket %d se desconectó\n", misClientes);
						FD_CLR(i, &conectados);
					}
					else {
						char *respuesta = "Handshake recibido";
						send(i, respuesta, strlen(respuesta) + 1, 0);
					}
				}
				else {
					if (recibirMensaje(i, misClientes) == 0) {
						cerrarConexion(i, "El socket %d se desconectó\n", misClientes);
						FD_CLR(i, &conectados);
						continue;
					}
					char *respuesta = "Mensaje recibido";
					send(i, respuesta, strlen(respuesta) + 1, 0);
				}
			}
        }
    }
    free(buffer);
    return 0;
}

void agregarCliente(char identificador, int socketCliente, miCliente *clientes) {
	int i;
	for (i=0; i<MAX_NUM_CLIENTES; i++) {
		if (clientes[i].socketCliente == -1) { //woohoo, encontramos un espacio libre
			clientes[i].socketCliente = socketCliente;
			clientes[i].identificador = identificador;
			break;
		}
	}
}

void borrarCliente(int socketCliente, miCliente *clientes) {
	int posicion = posicionSocket(socketCliente,clientes);
	if (posicion >= 0) {
		clientes[posicion].socketCliente = -1;
		clientes[posicion].identificador = 255;
	}
}


int enviarMensajeATodos(int socketCliente, char* mensaje, miCliente *clientes) {
	int cantidad = 0;
	int i;
	for (i=0;i<MAX_NUM_CLIENTES;i++) { //enviamos mensaje a los clientes registrados
		if (clientes[i].identificador >= MEMORIA && clientes[i].identificador <= CPU) { //solo le mandamos a MEMORIA,FILESYSTEM y CPU
			send(clientes[i].socketCliente, mensaje, strlen(mensaje), 0);
			cantidad++;
		}
	}
	return cantidad;
}

int recibirHandshake(int socketCliente, miCliente *clientes) {
	int buffersize = sizeof(headerDeLosRipeados);
	void *buffer = malloc(buffersize);
    int bytesRecibidos = recv(socketCliente, buffer, buffersize, 0);
	if (bytesRecibidos <= 0) {
		if (bytesRecibidos == -1) {
			logearError("El socket %d se desconectó\n", false, socketCliente);
		}
		else {
			logearError("Error en el recv\n",false);
		}
		return 0;
	}
	headerDeLosRipeados handy;
	deserializarHeader(&handy, buffer);
	free(buffer);

	int bytesDePayload = handy.bytesDePayload;
	int codigoDeOperacion = handy.codigoDeOperacion;

	if (bytesDePayload != 0) {
		logearError("La cantidad de bytes de payload de un handshake no puede ser distinto de 0", false);
	}

	if (CONSOLA <= codigoDeOperacion || codigoDeOperacion <= CPU) {
		int indice = posicionSocket(socketCliente,clientes);
    	if (indice < 0) {
    		logearInfo("El nuevo cliente fue identificado como: %s\n", ID_CLIENTE(codigoDeOperacion));
    		agregarCliente(codigoDeOperacion, socketCliente, clientes);
    		return 1;
    	}
	}
	return 0;
}

/*
 * Solo sirve para recibir el header de un mensaje, no un handshake.
 * Retorna la cantidad de bytes de payload.
 * Retorna -1 cuando hubo error.
 */
int recibirHeader(int socketCliente, miCliente *clientes) {
	int buffersize = sizeof(headerDeLosRipeados);
	void *buffer = malloc(buffersize);
    int bytesRecibidos = recv(socketCliente, buffer, buffersize, 0);
	if (bytesRecibidos <= 0) {
		return -1;
	}
	headerDeLosRipeados header;
	deserializarHeader(&header, buffer);
	free(buffer);

	int bytesDePayload = header.bytesDePayload;
	int codigoDeOperacion = header.codigoDeOperacion;

	if (codigoDeOperacion == MENSAJE) { // Si o si tiene que ser un mensaje
		return bytesDePayload;
	}
	logearInfo("Socket %d: Codigo de operacion invalida\n", socketCliente);
	return -1;
}

int recibirPayload(int socketCliente, int bytesDePayload, miCliente *clientes) {
    char* mensaje = malloc(bytesDePayload);
    int bytesRecibidos = recv(socketCliente, mensaje, bytesDePayload, 0);
    if (bytesRecibidos != -1) {
        logearInfo("Mensaje recibido: %s\n", mensaje);
        int cantidad = enviarMensajeATodos(socketCliente, mensaje, clientes);
        logearInfo("Mensaje retransmitido a %i clientes\n", cantidad);
    }
    else if (bytesRecibidos == 0) {
		logearError("El socket %d se desconectó\n", socketCliente);
	}
    else {
    	logearError("Error en el recv\n", false);
    }
    free(mensaje);
	return bytesRecibidos;
}


/*
 * Retorna la cantidad de bytes recibidos.
 * Retorna 0 cuando hubo error.
 */
int recibirMensaje(int socketCliente, miCliente *clientes) {
	int bytesDePayload = recibirHeader(socketCliente, clientes);
	if (bytesDePayload > 0) {
		int bytesRecibidos = recibirPayload(socketCliente, bytesDePayload + 1, clientes);
		return bytesRecibidos;
	}
	if (bytesDePayload == 0) {
        logearError("El cliente %i intentó mandar un mensaje sin contenido\n", false, socketCliente);
	}
	// bytesDePayload == -1
	return 0;
}

void limpiarClientes(miCliente *clientes) {
    int i;
    for(i = 0; i < MAX_NUM_CLIENTES; i++) {
        clientes[i].socketCliente = -1;
        clientes[i].identificador = 255;
    }
}

int existeCliente(int socketCliente, miCliente *clientes) {
	return (posicionSocket(socketCliente, clientes) != -1);
}

int posicionSocket(int socketCliente, miCliente *clientes) {
    int i;
    for(i = 0; i < MAX_NUM_CLIENTES; i++) {
        if(clientes[i].socketCliente == socketCliente) {
            return i;
        }
    }
    return -1;
}

void cerrarConexion(int socketCliente, char* motivo, miCliente *clientes) {
	logearInfo(motivo, socketCliente);
	borrarCliente(socketCliente, clientes);
	close(socketCliente);
}

void establecerConfiguracion() {
	if(config_has_property(config, "PUERTO_KERNEL")) {
		strcpy(PUERTO_KERNEL,config_get_string_value(config, "PUERTO_KERNEL"));
		logearInfo("Puerto Kernel: %s \n",PUERTO_KERNEL);
	} else {
		logearError("Error al leer el puerto del Kernel",true);
	}
}

