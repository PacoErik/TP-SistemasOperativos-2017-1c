#include <qepd/qepd.h>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include "commons/collections/list.h"

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

typedef t_list listaCliente;

void establecerConfiguracion();
void cerrarConexion(int, char*, listaCliente *);
void agregarCliente(char, int , listaCliente *);
void borrarCliente(int, listaCliente *);
int recibirMensaje(int, listaCliente *);
int recibirHandshake(int, listaCliente *);
int recibirHeader(int);
int recibirPayload(int, int, listaCliente *);
int existeCliente(int, listaCliente *);

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(void) {
	configurar("kernel");

	// miCliente misClientes[MAX_NUM_CLIENTES];
    // limpiarClientes(misClientes);

	listaCliente *misClientes = list_create();

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
    list_destroy_and_destroy_elements(misClientes, free);
    return 0;
}

#define DEF_MISMO_SOCKET(SOCKET)										\
		_Bool mismoSocket(void* elemento) {							\
			return SOCKET == ((miCliente *) elemento)->socketCliente;	\
		}																\

void agregarCliente(char identificador, int socketCliente, listaCliente *clientes) {
	if (existeCliente(socketCliente)) {
		logearError("No se puede agregar 2 veces mismo socket\n", false);
		return;
	}

	miCliente *cliente = malloc(sizeof (miCliente));
	cliente->identificador = identificador;
	cliente->socketCliente = socketCliente;

	list_add(clientes, cliente);
}

void borrarCliente(int socketCliente, listaCliente *clientes) {
	DEF_MISMO_SOCKET(socketCliente);
	list_remove_and_destroy_by_condition(clientes, mismoSocket, free);
}

int existeCliente(int socketCliente, listaCliente *clientes) {
	DEF_MISMO_SOCKET(socketCliente);
	return list_any_satisfy(clientes, mismoSocket);
}

int enviarMensajeATodos(int socketCliente, char* mensaje, listaCliente *clientes) {
	_Bool condicion(void* elemento) {
		miCliente *cliente = (miCliente*)elemento;
		return (cliente->identificador >= MEMORIA && cliente->identificador <= CPU);
	}

	listaCliente *clientesFiltrados = list_filter(clientes, condicion);

	void enviarMensaje(void* elemento) {
		miCliente *cliente = (miCliente*)elemento;
		send(cliente->socketCliente, mensaje, strlen(mensaje), 0);
	}

	list_iterate(clientesFiltrados, enviarMensaje);

	return list_size(clientesFiltrados);
}

int recibirHandshake(int socketCliente, listaCliente *clientes) {
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
		logearInfo("El nuevo cliente fue identificado como: %s\n", ID_CLIENTE(codigoDeOperacion));
		agregarCliente(codigoDeOperacion, socketCliente, clientes);
		return 1;
	}
	return 0;
}

/*
 * Solo sirve para recibir el header de un mensaje, no un handshake.
 * Retorna la cantidad de bytes de payload.
 * Retorna -1 cuando hubo error.
 */
int recibirHeader(int socketCliente) {
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

int recibirPayload(int socketCliente, int bytesDePayload, listaCliente *clientes) {
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
int recibirMensaje(int socketCliente, listaCliente *clientes) {
	int bytesDePayload = recibirHeader(socketCliente);
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

void cerrarConexion(int socketCliente, char* motivo, listaCliente *clientes) {
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

