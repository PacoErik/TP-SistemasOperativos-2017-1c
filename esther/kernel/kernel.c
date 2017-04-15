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
#include "commons/string.h"
#include <stdarg.h>

#define MOSTRAR_LOGS_EN_PANTALLA true

#define RUTA_CONFIG "config.cfg"
#define RUTA_LOG "kernel.log"

t_log* logger;
t_config* config;

char PUERTO_KERNEL[6];

#define MAX_NUM_CLIENTES 100

enum CodigoDeOperacion {
    MENSAJE, CONSOLA, MEMORIA, FILESYSTEM, CPU
};

#define ID_CLIENTE(x) ID_CLIENTES[x-1]
static const char *ID_CLIENTES[] = {
	"Consola", "Memoria", "File System", "CPU"
};

typedef struct headerDeLosRipeados {
    unsigned short bytesDePayload;
    char codigoDeOperacion; // 0 (mensaje). Handshake: 1 (consola), 2 (memoria), 3 (filesystem), 4 (cpu), 5 (kernel)
}__attribute__((packed, aligned(1))) headerDeLosRipeados;

typedef struct miCliente {
    short socketCliente;
    char identificador;
} miCliente;

void limpiarClientes(miCliente *);
void analizarCodigosDeOperacion(int , char , miCliente *);
int analizarHeader(int , char* , miCliente *);
void leerMensaje(int , short , miCliente *);
void establecerConfiguracion();
void configurar(char*);
void cerrarConexion(int , char* );
int posicionSocket(int , miCliente *);
void agregarCliente(char , int , miCliente *);
void borrarCliente(int , miCliente *);
void serializarHeader(headerDeLosRipeados *, char *);
void deserializarHeader(headerDeLosRipeados *, char *);
void logearInfo(char *, ...);
void logearError(char *, int, ...);
void handshake(int , char );

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
    char* buffer = malloc(buffersize);

    int bytesRecibidos;

    int i;

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

        int nuevoCliente; // Socket del nuevo cliente conectado
        struct sockaddr_in direccionCliente;

        for(i = 0; i <= fdmax; i++) {
        	if (FD_ISSET(i, &read_fds) == 0) {
        		continue;
        	}
			if (i == servidor) {
		        socklen_t addrlen = sizeof direccionCliente;
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
			else {
				if ((bytesRecibidos = recv(i, buffer, buffersize + 1, 0))
						<= 0) {
					if (bytesRecibidos == 0) {
						cerrarConexion(i, "El socket %d se desconectó\n");
					}
					else {
						logearError("Error en el recv\n",false);
						close(i);
					}
					borrarCliente(i, misClientes);
					FD_CLR(i, &conectados);
				}
				else {
					// llegó info, vamos a ver el header
					int estado = analizarHeader(i, buffer, misClientes);

					if (estado < 0) { //ocurrió algún problema con ese socket (puede ser un cliente o no)
						send(i, "Error, desconectado", 20, 0);
						borrarCliente(i, misClientes);
						cerrarConexion(i, "El socket %d hizo una operación inválida\n");
						FD_CLR(i, &conectados);
						continue; //salteamos esta iteración
					}
					char *respuesta = "Header recibido";
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

/**
 * Analiza el contenido del header, y respecto a ello realiza distintas acciones
 * devuelve -1 si el socket causa problemas
 */
int analizarHeader(int socketCliente, char* buffer, miCliente *clientes) {
    headerDeLosRipeados header;
    deserializarHeader(&header, buffer);
    int indice = posicionSocket(socketCliente,clientes);

    if (header.codigoDeOperacion >= CONSOLA && header.codigoDeOperacion <= CPU) {
    	// No estaba antes en el array de clientes
    	if (indice < 0) {
    		logearInfo("El nuevo cliente fue identificado como: %s\n", ID_CLIENTE(header.codigoDeOperacion));
    		agregarCliente(header.codigoDeOperacion,socketCliente,clientes);
    	}
    	else { //No se puede enviar un handshake 2 veces (el cliente ya estaba en el array de clientes)
    		return -1; // Otro cacho de código se va a encargar de borrarlo
    	}
    }

    else if (header.codigoDeOperacion == MENSAJE) {
        if (header.bytesDePayload <= 0) {
            logearError("El cliente %i intentó mandar un mensaje sin contenido\n",false,socketCliente);
            return -1;
        }
        else {
            leerMensaje(socketCliente, header.bytesDePayload, clientes);
        }
    }

    else {
    	if (indice >= 0) { //Si se encontró el cliente en la estructura de clientes (osea ya hizo handshake)
    		analizarCodigosDeOperacion(socketCliente, header.codigoDeOperacion, clientes); // TODO
    	}
    	else { //Header no reconocido, chau cliente intruso
    		return -1;
    	}
    }
    return 0;
}

int enviarMensajeATodos(int socketCliente, char* mensaje, miCliente *clientes) {
	int cantidad = 0;
	int i;
	for (i=0;i<MAX_NUM_CLIENTES;i++) { //enviamos mensaje a los clientes registrados
		if (clientes[i].identificador >= MEMORIA && clientes[i].identificador <= CPU) { //solo le mandamos a MEMORIA,FILESYSTEM y CPU
			send(clientes[i].socketCliente,mensaje,strlen(mensaje),0);
			cantidad++;
		}
	}
	return cantidad;
}

void leerMensaje(int socketCliente, short bytesDePayload, miCliente *clientes) {
    char* mensaje = malloc(bytesDePayload+1);
    recv(socketCliente,mensaje,bytesDePayload,0);
    mensaje[bytesDePayload]='\0';
    logearInfo("Mensaje recibido: %s\n",mensaje);
    int cantidad = enviarMensajeATodos(socketCliente,mensaje, clientes);
    logearInfo("Mensaje retransmitido a %i clientes\n",cantidad);
    free(mensaje);
}

void limpiarClientes(miCliente *clientes) {
    int i;
    for(i = 0; i < MAX_NUM_CLIENTES; i++) {
        clientes[i].socketCliente = -1;
        clientes[i].identificador = 255;
    }
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

void analizarCodigosDeOperacion(int socketCliente, char codigoDeOperacion, miCliente *clientes) {
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
            printf("TODO, cod. de operación recibido: %d\n",codigoDeOperacion);
            // TODO
    }
}

void cerrarConexion(int socketCliente, char* motivo){
	logearInfo(motivo, socketCliente);
	close(socketCliente);
}

void serializarHeader(headerDeLosRipeados *header, char *buffer) {
	short *pBytesDePayload = (short*) buffer;
	*pBytesDePayload = header->bytesDePayload;
	char *pCodigoDeOperacion = (char*)(pBytesDePayload + 1);
	*pCodigoDeOperacion = header->codigoDeOperacion;
}

void deserializarHeader(headerDeLosRipeados *header, char *buffer) {
	short *pBytesDePayload = (short*) buffer;
	header->bytesDePayload = *pBytesDePayload;
	char *pCodigoDeOperacion = (char*)(pBytesDePayload + 1);
	header->codigoDeOperacion = *pCodigoDeOperacion;
}

void logearInfo(char* formato, ...) {
	char* mensaje;
	va_list args;
	va_start(args, formato);
	mensaje = string_from_vformat(formato,args);
	log_info(logger,mensaje);
	printf("%s", mensaje);
	va_end(args);
}

void logearError(char* formato, int terminar , ...) {
	char* mensaje;
	va_list args;
	va_start(args, terminar);
	mensaje = string_from_vformat(formato,args);
	log_error(logger,mensaje);
	printf("%s", mensaje);
	va_end(args);
	if (terminar==true) {
		exit(EXIT_FAILURE);
	}
}

void establecerConfiguracion() {
	if(config_has_property(config, "PUERTO_KERNEL")) {
		strcpy(PUERTO_KERNEL,config_get_string_value(config, "PUERTO_KERNEL"));
		logearInfo("Puerto Kernel: %s \n",PUERTO_KERNEL);
	} else {
		logearError("Error al leer el puerto del Kernel",true);
	}
}

int existeArchivo(const char *ruta) {
    FILE *archivo;
    if ((archivo = fopen(ruta, "r")))
    {
        fclose(archivo);
        return true;
    }
    return false;
}

void configurar(char* quienSoy) {

	//Esto es por una cosa rara del Eclipse que ejecuta la aplicación
	//como si estuviese en la carpeta esther/consola/
	//En cambio, en la terminal se ejecuta desde esther/consola/Debug
	//pero en ese caso no existiria el archivo config ni el log
	//y es por eso que tenemos que leerlo desde el directorio anterior

	if (existeArchivo(RUTA_CONFIG)) {
		config = config_create(RUTA_CONFIG);
		logger = log_create(RUTA_LOG, quienSoy, false, LOG_LEVEL_INFO);
	} else {
		config = config_create(string_from_format("../%s",RUTA_CONFIG));
		logger = log_create(string_from_format("../%s",RUTA_LOG), quienSoy,false, LOG_LEVEL_INFO);
	}

	//Si la cantidad de valores establecidos en la configuración
	//es mayor a 0, entonces configurar la ip y el puerto,
	//sino, estaría mal hecho el config.cfg

	if(config_keys_amount(config) > 0) {
		establecerConfiguracion();
	} else {
		logearError("Error al leer archivo de configuración",true);
	}
	config_destroy(config);
}

void handshake(int socket, char operacion) {
	logearInfo("Conectando a servidor 0%%\n");
	headerDeLosRipeados handy;
	handy.bytesDePayload = 0;
	handy.codigoDeOperacion = operacion;

	int buffersize = sizeof(headerDeLosRipeados);
	char *buffer = malloc(buffersize);
	serializarHeader(&handy, buffer);
	send(socket, (void*) buffer, buffersize, 0);

	char respuesta[1024];
	int bytesRecibidos = recv(socket, (void *) &respuesta, sizeof(respuesta), 0);

	if (bytesRecibidos > 0) {
		logearInfo("Conectado a servidor 100 porciento\n");
		logearInfo("Mensaje del servidor: \"%s\"\n", respuesta);
	}
	else {
		logearError("Ripeaste\n",true);
	}
	free(buffer);
}
