//-----HEADERS-----//
#include <qepd/qepd.h>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include "commons/collections/list.h"
#include <string.h>

//-----DEFINES-----//
#define MAX_NUM_CLIENTES 100
#define ID_CLIENTE(x) ID_CLIENTES[x]
#define DEF_MISMO_SOCKET(SOCKET)										\
		_Bool mismoSocket(void* elemento) {							\
			return SOCKET == ((miCliente *) elemento)->socketCliente;	\
		}

//-----ESTRUCTURAS-----//
typedef struct datosMemoria{  // mem?
	int pid;
	char *code;
	short int codeSize;
}__attribute__((packed, aligned(1))) datosMemoria;
typedef struct miCliente {
    short socketCliente;
    char identificador;
} miCliente;
typedef t_list listaCliente;
typedef t_list listaProcesos;
typedef struct PCB {
	int PID;
	int PC;
	int ERROR_NUM;
} PCB;

//Un choclazo de struct para llevar la estadística de los procesos y tener
//un control más agrupado.. o algo así :v
typedef struct Proceso {
	int consola; //Socket la consola que envio este proceso
	int cantRafagas;
	int cantOperacionesPriv;
	int cantPaginasHeap;
	int cantAlocar;
	int bytesAlocados;
	int cantLiberar;
	int bytesLiberados;
	int cantSyscalls; //sí, me encantan los int
	PCB pcb;
} Proceso;

//-----VARIABLES GLOBALES-----//
t_log* logger;
t_config* config;
listaCliente *clientes;
listaProcesos *procesos;
static const char *ID_CLIENTES[] = {"Consola", "Memoria", "File System", "CPU"};
char PUERTO_KERNEL[6];
char IP_MEMORIA[16];
int PUERTO_MEMORIA;
char IP_FS[16];
int PUERTO_FS;
int QUANTUM;
int QUANTUM_SLEEP;
char ALGORITMO[5];
int GRADO_MULTIPROG;
//SEM_IDS
//SEM_INIT
//SHARED_VARS
int STACK_SIZE;
int PID_GLOBAL; //A modo de prueba el PID va a ser un simple contador
int memoria; //el socket de la memoria

//-----PROTOTIPOS DE FUNCIONES-----//
void 	hacerPedidoMemoria(datosMemoria);
void 	agregarCliente(char, int);
void 	agregarProceso(int, int, int);
void 	borrarCliente(int);
void 	cerrarConexion(int, char*);
void	eliminarProceso(int);
void 	enviarHeader(int, char, int);
int 	enviarMensajeATodos(int, char*);
void 	establecerConfiguracion();
int 	existeCliente(int);
int		existeProceso(int PID);
void* 	get_in_addr(struct sockaddr *);
void 	procesarMensaje(int, char, int);
int 	recibirHandshake(int);
int 	recibirHeader(int, headerDeLosRipeados*);
int 	recibirMensaje(int, int);
int 	tipoCliente(int);

//-----PROCEDIMIENTO PRINCIPAL-----//
int main(void) {
	configurar("kernel");

	clientes = list_create();
	procesos = list_create();

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
        logearError("No se pudo abrir el server",true);
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
        logearError("Fallo al bindear el puerto",true);
    }

    freeaddrinfo(direcciones); // No necesito mas la lista de direcciones

    if (listen(servidor, 10) == -1) {
        logearError("Fallo al escuchar",true);
    }
    logearInfo("Estoy escuchando");

    fd_set conectados; // Set de FDs conectados
    fd_set read_fds; // sockets de lectura

    FD_ZERO(&conectados);
    FD_ZERO(&read_fds);

    int fdmax;	// valor maximo de los FDs

    conectar(&memoria,IP_MEMORIA,PUERTO_MEMORIA);
    handshake(memoria,KERNEL); //KERNEL = 4
    agregarCliente(MEMORIA, memoria);

    fdmax = memoria>servidor?memoria:servidor; //la pajilla mental

    FD_SET(servidor, &conectados);
    FD_SET(fileno(stdin), &conectados);
    FD_SET(memoria, &conectados);

    for(;;) {
        read_fds = conectados;
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            logearError("Error en el select",true);
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
					logearError("Fallo en el accept",false);
				}
				else {
					FD_SET(nuevoCliente, &conectados); // Agregarlo al set
					if (nuevoCliente > fdmax) {
						fdmax = nuevoCliente; // Cambia el maximo
					}
				    char direccionIP[INET_ADDRSTRLEN]; // string que contiene la direccion IP del cliente
					inet_ntop(AF_INET,	get_in_addr((struct sockaddr*) &direccionCliente), direccionIP, INET_ADDRSTRLEN);
					logearInfo("Nueva conexión desde %s en el socket %d", direccionIP, nuevoCliente);
				}
			}
			//Mensaje por interfaz del Kernel
			else if (i == fileno(stdin)) {
				//Esto es solo testeo, para probar que efectivamente se puede
				//tener input a la vez de recibir clientes y toda esa wea
				char str[16];
				scanf("%s",&str);
				printf("%s\n",str);
			}
			// Un cliente mando un mensaje
			else {
				if (existeCliente(i) == 0) { // Nuevo cliente, debe enviar un handshake
					if (recibirHandshake(i) == 0) {
						cerrarConexion(i, "El socket %d se desconectó\n");
						FD_CLR(i, &conectados);
					}
					else {
						char *respuesta = "Bienvenido!";
						send(i, respuesta, strlen(respuesta) + 1, 0);
					}
				}
				else {
					//Recibir header
					headerDeLosRipeados header;
					int bytesRecibidos = recibirHeader(i, &header);
					if (bytesRecibidos <= 0) {
						int tipo = tipoCliente(i);
						if (tipo >= 0) {
							logearInfo("Cliente [%s] desconectado",ID_CLIENTE(tipo));
							borrarCliente(i);
							close(i);
						} else {
							cerrarConexion(i, "El socket %d se desconectó\n");
						}
						FD_CLR(i, &conectados);
						free(buffer);
						continue;
					}

					int bytesDePayload = header.bytesDePayload;
					int codigoDeOperacion = header.codigoDeOperacion;

					//Procesar operación del header
					procesarMensaje(i,codigoDeOperacion,bytesDePayload);
				}
			}
        }
    }
    free(buffer);
    list_destroy_and_destroy_elements(clientes, free);
    return 0;
}

//-----DEFINICIÓN DE FUNCIONES-----//
void hacerPedidoMemoria(datosMemoria datosMem) {
	//Segun entendemos, en el main ya se conecta no se por que, dejamos eso por las dudas cmentado, el send lo hago de una



	// Primer send

	int tamanioTotal = sizeof(int) + sizeof(datosMem.codeSize) + datosMem.codeSize;

	enviarHeader(memoria, INICIAR_PROGRAMA, tamanioTotal);
	// Segundo send
	char *buffer = malloc(tamanioTotal); // tamanio del codigo + 4 bytes del pid + tamanio del (datosMem.codeSize) esto ultimo lo uso en memoria para saber cuanto codigo leer del buffer
	memcpy(buffer,&datosMem.pid,sizeof(int)); // Como no tengo puntero del pid (de code si), lo paso con &
	memcpy(buffer+sizeof(int),&datosMem.codeSize,sizeof(datosMem.codeSize)); // Aca termino de llenar el buffer que voy a mandar, copie pid primero y dsps codigo
	memcpy(buffer+sizeof(int)+sizeof(datosMem.codeSize),datosMem.code,datosMem.codeSize);
	send(memoria, buffer, tamanioTotal, 0);

	free(buffer);
	//
}


void agregarCliente(char identificador, int socketCliente) {
	if (existeCliente(socketCliente)) {
		logearError("No se puede agregar 2 veces mismo socket", false);
		return;
	}

	miCliente *cliente = malloc(sizeof (miCliente));
	cliente->identificador = identificador;
	cliente->socketCliente = socketCliente;

	list_add(clientes, cliente);
}
void agregarProceso(int pid, int PC, int error) {
	if (existeProceso(pid)) {
		printf("Warning: Ya existe proceso con mismo PID\n");
	}
	PCB *proceso = malloc(sizeof(PCB));
	proceso->PID = pid;
	proceso->PC = PC;
	proceso->ERROR_NUM = error;
	list_add(procesos, proceso);
}
void borrarCliente(int socketCliente) {
	DEF_MISMO_SOCKET(socketCliente);
	list_remove_and_destroy_by_condition(clientes, mismoSocket, free);
}
void cerrarConexion(int socketCliente, char* motivo) {
	logearError(motivo, false, socketCliente);
	borrarCliente(socketCliente);
	close(socketCliente);
}
void eliminarProceso(int PID) {
	_Bool mismoPID(void* elemento) {
		return PID == ((PCB*) elemento)->PID;
	}
	list_remove_and_destroy_by_condition(procesos, mismoPID, free);
}
int enviarMensajeATodos(int socketCliente, char* mensaje) {
	_Bool condicion(void* elemento) {
		miCliente *cliente = (miCliente*)elemento;
		return (cliente->identificador >= MEMORIA && cliente->identificador <= CPU);
	}

	listaCliente *clientesFiltrados = list_filter(clientes, condicion);

	void enviarMensaje(void* elemento) {
		miCliente *cliente = (miCliente*)elemento;
		enviarHeader(cliente->socketCliente, MENSAJE, strlen(mensaje));
		send(cliente->socketCliente, mensaje, strlen(mensaje), 0);
	}

	list_iterate(clientesFiltrados, enviarMensaje);

	return list_size(clientesFiltrados);
}
void establecerConfiguracion() {
	strcpy(PUERTO_KERNEL,config_get_string_value(config, "PUERTO_KERNEL"));
	logearInfo("Puerto Kernel: %s",PUERTO_KERNEL);

	strcpy(IP_MEMORIA,config_get_string_value(config, "IP_MEMORIA"));
	logearInfo("IP Memoria: %s",IP_MEMORIA);

	PUERTO_MEMORIA = config_get_int_value(config, "PUERTO_MEMORIA");
	logearInfo("Puerto Memoria: %d", PUERTO_MEMORIA);

	strcpy(IP_FS,config_get_string_value(config, "IP_FS"));
	logearInfo("IP File System: %s",IP_FS);

	PUERTO_FS = config_get_int_value(config, "PUERTO_FS");
	logearInfo("Puerto File System: %d", PUERTO_FS);

	QUANTUM = config_get_int_value(config, "QUANTUM");
	logearInfo("QUANTUM: %d", QUANTUM);

	QUANTUM_SLEEP = config_get_int_value(config, "QUANTUM_SLEEP");
	logearInfo("QUANTUM_SLEEP: %d", QUANTUM_SLEEP);

	strcpy(ALGORITMO,config_get_string_value(config, "ALGORITMO"));
	logearInfo("ALGORITMO: %s",ALGORITMO);

	GRADO_MULTIPROG = config_get_int_value(config, "GRADO_MULTIPROG");
	logearInfo("GRADO_MULTIPROG: %d", GRADO_MULTIPROG);

	STACK_SIZE = config_get_int_value(config, "STACK_SIZE");
	logearInfo("STACK_SIZE: %d", STACK_SIZE);
}
int existeCliente(int socketCliente) {
	DEF_MISMO_SOCKET(socketCliente);
	return list_any_satisfy(clientes, mismoSocket);
}
int existeProceso(int PID) {
	_Bool mismoPID(void* elemento) {
		return PID == ((PCB*) elemento)->PID;
	}
	return list_any_satisfy(procesos, mismoPID);
}
void* get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}
void procesarMensaje(int socketCliente, char operacion, int bytes) {

	int tipo = tipoCliente(socketCliente);

	switch(tipo) {
		case CONSOLA:
			if (operacion == MENSAJE) {
				recibirMensaje(socketCliente,bytes);
			}
			else if (operacion == INICIAR_PROGRAMA) {
				char* codigo = malloc(bytes+1);
				recv(socketCliente, codigo, bytes, 0);
				codigo[bytes]='\0';

				int nuevo_PID;
				if (list_size(procesos) < GRADO_MULTIPROG) {
					PID_GLOBAL++;
					//TODO: delegar a la memoria el agregado de nuestro proceso
					//Quizás haya un error al alocarlo, por lo que el proceso
					//puede terminar luego de la petición a la memoria.
					nuevo_PID = PID_GLOBAL;
					datosMemoria datosMem;
					datosMem.codeSize = strlen(codigo);
					datosMem.pid=nuevo_PID;
					datosMem.code = malloc(datosMem.codeSize);
					strcpy(datosMem.code,codigo); //Como codigo es un string, estoy copiandolo a un array de chars, que es otro string. Por eso no uso &
					hacerPedidoMemoria(datosMem);
					free(datosMem.code);
					///////// hasta acá memoria
					agregarProceso(nuevo_PID,0,1);
					printf("Proceso agregado con PID: %d\n",PID_GLOBAL);
					// Le envia el PID a la consola
					//enviarHeader(socketCliente, INICIAR_PROGRAMA, sizeof(nuevo_PID));
					//send(socketCliente, &nuevo_PID, sizeof(nuevo_PID), 0);
				} else {
					printf("No se pudo añadir proceso\n");
					enviarHeader(socketCliente, ERROR_MULTIPROGRAMACION, 0);
				}

			}
			else if (operacion == FINALIZAR_PROGRAMA) {
				int PID;
				recv(socketCliente, &PID, sizeof PID, 0);
				logearInfo("Pedido de finalizacion de PID %d", PID);
				if (existeProceso(PID)) {
					eliminarProceso(PID);
					if (!existeProceso(PID))
						logearInfo("PID %d Eliminado", PID);
				}
				else {
					logearError("No existe PID %d", false, PID);
					break;
				}
			}
			break;

		case MEMORIA:
			printf("Memoria\n");
			break;

		case FILESYSTEM:
			printf("File System\n");
			break;

		case CPU:
			printf("CPU\n");
			break;

		default:
			logearError("Operación inválida de %s", false, ID_CLIENTE(tipo));
			break;
	}
}

int recibirHeader(int socket, headerDeLosRipeados *header) {
	int buffersize = sizeof(headerDeLosRipeados);
	void *buffer = malloc(buffersize);
	int bytesRecibidos = recv(socket, buffer, buffersize, 0);

	deserializarHeader(header, buffer);
	free(buffer);

	return bytesRecibidos;
}

int recibirHandshake(int socketCliente) {
	headerDeLosRipeados handy;

	recibirHeader(socketCliente, &handy);

	if (CONSOLA <= handy.codigoDeOperacion
			&& handy.codigoDeOperacion <= CPU
			&& handy.bytesDePayload == 0) {
		logearInfo("El nuevo cliente fue identificado como: %s", ID_CLIENTE(handy.codigoDeOperacion));
		agregarCliente(handy.codigoDeOperacion, socketCliente);
		return 1;
	}
	return 0;
}

int recibirMensaje(int socketCliente, int bytesDePayload) {
    char* mensaje = malloc(bytesDePayload+1);
    int bytesRecibidos = recv(socketCliente, mensaje, bytesDePayload, 0);
    mensaje[bytesDePayload]='\0';
    if (bytesRecibidos > 0) {
        logearInfo("Mensaje recibido: %s", mensaje);
        int cantidad = enviarMensajeATodos(socketCliente, mensaje);
        logearInfo("Mensaje retransmitido a %i clientes", cantidad);
    } else {
    	cerrarConexion(socketCliente,"Error al recibir mensaje del socket %i\n");
    }
    free(mensaje);
	return bytesRecibidos;
}
int tipoCliente(int socketCliente) {
	DEF_MISMO_SOCKET(socketCliente);
	miCliente *found = (miCliente*)(list_find(clientes, mismoSocket));
	if (found == NULL) {
		return -1;
	}
	return found->identificador;
}
