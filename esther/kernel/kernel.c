//-----HEADERS-----//
#include <qepd/qepd.h>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include "commons/collections/list.h"
#include "commons/collections/queue.h"
#include <string.h>
#include "parser/metadata_program.h"
#include "commons/collections/list.h"

#include "op_memoria.h"

//-----DEFINES-----//
#define ID_CLIENTE(x) ID_CLIENTES[x-CONSOLA]
#define DEF_MISMO_SOCKET(SOCKET)										\
		_Bool mismoSocket(void* elemento) {							\
			return SOCKET == ((miCliente *) elemento)->socketCliente;	\
		}

//-----ESTRUCTURAS-----//
typedef struct datosMemoria {
	int pid;
	short int codeSize;
	char *code;
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
	t_size			instrucciones_size;				// Cantidad de instrucciones
	t_intructions*	instrucciones_serializado;
	t_size			etiquetas_size;					// Tamaño del mapa serializado de etiquetas
	char*			etiquetas;
	int				cantidad_de_funciones;
	int				cantidad_de_etiquetas;
} PCB;

//Un choclazo de struct para llevar la estadística de los procesos y tener
//un control más agrupado.. o algo así :v
typedef struct Proceso {
	int consola; //Socket de la consola que envio este proceso
	int cantRafagas;
	int cantOperacionesPriv;
	int cantPaginasHeap;
	int cantAlocar;
	int bytesAlocados;
	int cantLiberar;
	int bytesLiberados;
	int cantSyscalls; //sí, me encantan los int
	char* codigo;
	PCB pcb;
} Proceso;

//-----VARIABLES GLOBALES-----//
t_log* logger;
t_config* config;

listaCliente *clientes;

t_queue* cola_NEW;
listaProcesos *procesos; //Dentro van a estar los procesos en estado READY/EXEC/BLOCKED
t_list* lista_EXIT;

static const char *ID_CLIENTES[] = {"Consola", "Memoria", "File System", "CPU"};
int PUERTO_KERNEL;
char IP_FS[16];
int PUERTO_FS;
int QUANTUM;
int QUANTUM_SLEEP;
char ALGORITMO[5];
int GRADO_MULTIPROG;
//SEM_IDS
//SEM_INIT
//SHARED_VARS
int PID_GLOBAL; //A modo de prueba el PID va a ser un simple contador

/* op_memoria.h */
int socket_memoria;
int tamanio_pagina;
char IP_MEMORIA[16];
int PUERTO_MEMORIA;
int STACK_SIZE;


//-----PROTOTIPOS DE FUNCIONES-----//

void 		agregarCliente(char, int);
void 		agregarProceso(Proceso *);
void 		borrarCliente(int);
int 		cantidadProcesosEnSistema();
void 		cerrarConexion(int, char*);
void		eliminarProceso(int);
int 		enviarMensajeATodos(int, char*);
void 		establecerConfiguracion();
int 		existeCliente(int);
int			existeProceso(int PID);
void 		finalizar_programa(int PID);
void 		hacerPedidoMemoria(datosMemoria);
inline void imprimirOpcionesDeKernel();
void 		intentar_iniciar_proceso();
void 		interaccionKernel();
inline void limpiarBufferEntrada();
void 		procesarMensaje(int, char, int);
int 		recibirHandshake(int);
int 		recibirMensaje(int, int);
char*		removerSaltoDeLinea(char*);
int			soloNumeros(char*);
int 		tipoCliente(int);

//-----PROCEDIMIENTO PRINCIPAL-----//
int main(void) {
	configurar("kernel");

	clientes = list_create();

	cola_NEW = queue_create();
	procesos = list_create();
	lista_EXIT = list_create();

	int servidor = socket(AF_INET, SOCK_STREAM, 0);

	if (servidor == -1) {
		logearError("No se pudo crear el socket", true);
	}

	int activado = 1;
	setsockopt(servidor, SOL_SOCKET, SO_REUSEADDR, &activado, sizeof(activado));

	struct sockaddr_in servidor_info;

	servidor_info.sin_family = AF_INET;
	servidor_info.sin_port = htons(PUERTO_KERNEL);
	servidor_info.sin_addr.s_addr = INADDR_ANY;
	bzero(&(servidor_info.sin_zero), 8);

	if (bind(servidor, (struct sockaddr*) &servidor_info, sizeof(struct sockaddr)) == -1) {
		logearError("Fallo al bindear el puerto", true);
	}

    if (listen(servidor, 10) == -1) {
		logearError("Fallo al escuchar", true);
    }

    logearInfo("Estoy escuchando");

    fd_set conectados;		// Set de FDs conectados
    fd_set read_fds;		// sockets de lectura

    FD_ZERO(&conectados);
    FD_ZERO(&read_fds);

    int fdmax;				// valor maximo de los FDs

    if (mem_conectar() == 0) {
    	logearError("No se pudo conectar a la memoria.", true);
    }

	agregarCliente(MEMORIA, socket_memoria);

	fdmax = (socket_memoria > servidor) ? socket_memoria : servidor; // Maximo entre fd de memoria y kernel

    FD_SET(servidor, &conectados);
    FD_SET(socket_memoria, &conectados);
    FD_SET(fileno(stdin), &conectados);

    imprimirOpcionesDeKernel();

    for(;;) {
        read_fds = conectados;

		if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
			logearError("Error en el select", true);
        }

        struct sockaddr_in cliente_info;

        int i;
        for(i = 0; i <= fdmax; i++) {
        	if (FD_ISSET(i, &read_fds) == 0) { // No hubo cambios en este socket
        		continue;
        	}

        	// Se detecta un cambio en el socket de escucha => hay un nuevo cliente
			if (i == servidor) {
		        int nuevoCliente;			// Socket del nuevo cliente conectado

		        socklen_t addrlen = sizeof cliente_info;
				nuevoCliente = accept(servidor, (struct sockaddr*) &cliente_info, &addrlen);

				if (nuevoCliente == -1) {
					logearError("Fallo en el accept", false);
				}
				else {
					FD_SET(nuevoCliente, &conectados);		// Agregarlo al set
					if (nuevoCliente > fdmax) {
						fdmax = nuevoCliente;				// Cambia el maximo
					}
					logearInfo("Nueva conexión desde %s en el socket %d",
							inet_ntoa(cliente_info.sin_addr), nuevoCliente);
				}
			}
			// Mensaje por interfaz del Kernel
			else if (i == fileno(stdin)) {
				interaccionKernel();
			}
			// Un cliente mando un mensaje
			else {
				if (existeCliente(i) == 0) {			// Nuevo cliente, debe enviar un handshake
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
							logearInfo("Cliente [%s] desconectado", ID_CLIENTE(tipo));
							borrarCliente(i);
							close(i);
						} else {
							cerrarConexion(i, "El socket %d se desconectó\n");
						}
						FD_CLR(i, &conectados);
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
    list_destroy_and_destroy_elements(clientes, free);
    return 0;
}

//-----DEFINICIÓN DE FUNCIONES-----//
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
void agregarProceso(Proceso *nuevo_proceso) {
	int pid = nuevo_proceso->pcb.PID;
	if (existeProceso(pid)) {
		printf("Warning: Ya existe proceso con mismo PID\n");
	}
	list_add(procesos, nuevo_proceso);
}
void borrarCliente(int socketCliente) {
	DEF_MISMO_SOCKET(socketCliente);
	list_remove_and_destroy_by_condition(clientes, mismoSocket, free);
}
int cantidadProcesosEnSistema() {
	return list_size(procesos);
}
void cerrarConexion(int socketCliente, char* motivo) {
	logearError(motivo, false, socketCliente);
	borrarCliente(socketCliente);
	close(socketCliente);
}
void eliminarProceso(int PID) {
	_Bool mismoPID(void* elemento) {
		return PID == ((Proceso*) elemento)->pcb.PID;
	}

	Proceso* proceso = list_remove_by_condition(procesos,mismoPID);

	free(proceso->codigo);
	free(proceso->pcb.etiquetas);
	free(proceso->pcb.instrucciones_serializado);

	//Aún tiene memoria reservada para los otros campos.
	//Pero como dicen que hay que mantener la traza de ejecución
	//no los libero, ya que nos interesan saber las estadísticas de los
	//procesos finalizados.

	list_add(lista_EXIT,proceso);
}
int enviarMensajeATodos(int socketCliente, char* mensaje) {
	_Bool condicion(void* elemento) {
		miCliente *cliente = (miCliente*)elemento;
		return (cliente->identificador >= MEMORIA && cliente->identificador <= CPU);
	}

	listaCliente *clientesFiltrados = list_filter(clientes, condicion);

	void enviarMensaje(void* elemento) {
		miCliente *cliente = (miCliente*)elemento;
		if (cliente->identificador == MEMORIA) {
			mem_mensaje(mensaje);
			return;
		}

		enviarHeader(cliente->socketCliente, MENSAJE, strlen(mensaje));
		send(cliente->socketCliente, mensaje, strlen(mensaje), 0);

	}

	list_iterate(clientesFiltrados, enviarMensaje);

	return list_size(clientesFiltrados);
}
void establecerConfiguracion() {
	PUERTO_KERNEL = config_get_int_value(config, "PUERTO_KERNEL");
	logearInfo("Puerto Kernel: %d",PUERTO_KERNEL);

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
		return PID == ((Proceso*) elemento)->pcb.PID;
	}
	return list_any_satisfy(procesos, mismoPID);
}
void finalizar_programa(int PID) {
	if (existeProceso(PID)) {
		eliminarProceso(PID);
		logearInfo("[PID:%d] Eliminado", PID);
	}
	else {
		logearError("[PID:%d] No existe PID", false, PID);
	}
}
void hacerPedidoMemoria(datosMemoria datosMem) {
	int tamanioTotal = sizeof(int) + sizeof(datosMem.codeSize) + datosMem.codeSize;

	enviarHeader(socket_memoria, INICIAR_PROGRAMA, tamanioTotal);

	char *buffer = malloc(tamanioTotal);				// Tamanio del codigo

	memcpy(buffer, &datosMem.pid, sizeof(int));		// Como no tengo puntero del pid (de code si), lo paso con &
	memcpy(buffer + sizeof(int), &datosMem.codeSize, sizeof(datosMem.codeSize)); // Aca termino de llenar el buffer que voy a mandar, copie pid primero y dsps codigo
	memcpy(buffer + sizeof(int) + sizeof(datosMem.codeSize), datosMem.code, datosMem.codeSize);

	send(socket_memoria, buffer, tamanioTotal, 0);

	free(buffer);
}
inline void imprimirOpcionesDeKernel() {
	printf(
			"\n--------------------\n"
			"BIENVENIDO AL KERNEL\n"
			"Comandos disponibles: \n\n"
			"listado"
				"\t//Ver procesos de todas las colas\n"
			"listado [NEW/READY/EXEC/BLOCKED/EXIT]"
				"\t//Ver procesos en una cola específica\n"
			"proceso [PID]"
				"\t//Ver información del proceso PID\n"
			"tablaglobal"
				"\t//Ver la tabla global de archivos\n"
			"multiprogramacion [GRADO]"
				"\t//Cambiar el grado de multiprogramación a GRADO\n"
			"finalizar [PID]"
				"\t//Finaliza el proceso PID\n"
			"detener"
				"\t//Detiene la planificación\n"
			"planificar"
				"\t//Inicia la planificación\n"
			"opciones"
				"\t//Mostrar opciones\n"
	);
}
void intentar_iniciar_proceso() {
	if (cantidadProcesosEnSistema() < GRADO_MULTIPROG) {
		Proceso* nuevo_proceso = queue_pop(cola_NEW);
		if (nuevo_proceso == NULL) {
			logearInfo("No hay procesos en la cola NEW");
		}
		else {
			char *codigo = strdup(nuevo_proceso->codigo);
			int PID = nuevo_proceso->pcb.PID;

			int ret = mem_inicializar_programa(PID, strlen(codigo), codigo);
			if (ret == -1) {
				logearError("Error de conexion con la memoria.", true);
			}

			printf("Pedido a memoria\n");
			free(codigo);

			if (ret == 0) {
				logearError("[PID %d] Memoria insuficiente.", false, PID);
				queue_push(cola_NEW, nuevo_proceso); // Vuelve a la cola de new
				PID = -1;
				enviarHeader(nuevo_proceso->consola, INICIAR_PROGRAMA, sizeof(PID));
				send(nuevo_proceso->consola, &PID, sizeof(PID), 0);
			}

			else {
				agregarProceso(nuevo_proceso);

				printf("Proceso agregado con PID: %d\n",PID);

				// Le envia el PID a la consola
				enviarHeader(nuevo_proceso->consola, INICIAR_PROGRAMA, sizeof(PID));
				send(nuevo_proceso->consola, &PID, sizeof(PID), 0);
			}
		}
	}
}
void interaccionKernel() {
	struct comando {
		char *nombre;
		void (*funcion) (char *param);
	};

	void listado(char *estado) {
		logearInfo("Comando de listado de programas ejecutado");
		string_trim(&estado);
		if (strlen(estado) == 0) {
			logearInfo("[Listado] Todos:");
		} else if (strcmp("NEW",estado)==0) {
			logearInfo("[Listado] NEW:");
		} else if (strcmp("READY",estado)==0) {
			logearInfo("[Listado] READY:");
		} else if (strcmp("EXEC",estado)==0) {
			logearInfo("[Listado] EXEC:");
		} else if (strcmp("BLOCKED",estado)==0) {
			logearInfo("[Listado] BLOCKED:");
		} else if (strcmp("EXIT",estado)==0) {
			logearInfo("[Listado] EXIT:");
		} else {
			logearError("[Listado] Parámetro desconocido", false);
		}
		free(estado);
	}

	void proceso(char *param) {
		logearInfo("[Proceso]");
	}

	void tablaglobal(char *param) {
		logearInfo("[Tabla Global]");
	}

	void multiprogramacion(char *param) {
		logearInfo("[Multiprogramación]");
	}

	void finalizar(char *sPID) {
		logearInfo("Comando de desconexión de programa ejecutado");

		string_trim(&sPID);

		if (strlen(sPID) == 0) {
			logearError("El comando \"finalizar\" recibe un parametro [PID]", false);
			free(sPID);
			return;
		}

		if (!soloNumeros(sPID)) {
			logearError("Error: \"%s\" no es un PID valido!", false, sPID);
			free(sPID);
			return;
		}

		int PID = strtol(sPID, NULL, 0);
		free(sPID);

		logearInfo("Proceso %d finalizado",PID);
	}

	void detener(char *param) {
		logearInfo("[Detener]");
	}

	void planificar(char *param) {
		logearInfo("[Planificar]");
	}

	void opciones(char *param) {
		string_trim(&param);
		if (strlen(param) != 0) {
			logearError("El comando \"opciones\" no recibe nungun parametro", false);
			free(param);
			return;
		}
		free(param);
		imprimirOpcionesDeKernel();
	}

	struct comando comandos[] = {
		{ "listado", listado },
		{ "proceso", proceso },
		{ "tablaglobal", tablaglobal },
		{ "multiprogramacion", multiprogramacion },
		{ "finalizar", finalizar },
		{ "detener", detener },
		{ "planificar", planificar },
		{ "opciones" , opciones }
	};

	char input[100];
	memset(input, 0, sizeof input);
	fgets(input, sizeof input, stdin);

	if (strlen(input) == 1) {
		return;
	}

	if (input[strlen(input) - 1] != '\n') {
		logearError("Un comando no puede tener mas de 100 digitos", false);
		limpiarBufferEntrada();
		return;
	}

	removerSaltoDeLinea(input);

	char *inputline = strdup(input); // Si no hago eso, string_trim se rompe
	string_trim_left(&inputline); // Elimino espacios a la izquierda

	char *cmd = NULL; // Comando
	char *save = NULL; // Apunta al primer caracter de los siguentes caracteres sin parsear
	char *delim = " ,"; // Separador

	// strtok_r(3) devuelve un token leido
	cmd = strtok_r(inputline, delim, &save); // Comando va a ser el primer token

	int i;
	// La division de los sizeof me calcula la cantidad de comandos
	for (i = 0; i < (sizeof comandos / sizeof *comandos); i++) {
		char *_cmd = comandos[i].nombre;
		if (strcmp(cmd, _cmd) == 0) {
			char *param = strdup(save); // Para no pasarle save por referencia
			comandos[i].funcion(param);
			break;
		}
	}
	if (i == (sizeof comandos / sizeof *comandos)) {
		logearError("Error: %s no es un comando", false, cmd);
	}
}
inline void limpiarBufferEntrada() {
	int c;
	while ((c = getchar()) != '\n' && c != EOF);
}
void procesarMensaje(int socketCliente, char operacion, int bytes) {

	int tipo = tipoCliente(socketCliente);

	switch(tipo) {
		case CONSOLA:
			if (operacion == MENSAJE) {
				recibirMensaje(socketCliente,bytes);
			}
			else if (operacion == INICIAR_PROGRAMA) {
				PID_GLOBAL++;
				char* codigo = malloc(bytes+1);
				recv(socketCliente, codigo, bytes, 0);
				codigo[bytes]='\0';
				Proceso* nuevo_proceso = malloc(sizeof(Proceso));
				nuevo_proceso->consola = socketCliente;
				nuevo_proceso->bytesAlocados = 0;
				nuevo_proceso->bytesLiberados = 0;
				nuevo_proceso->cantAlocar = 0;
				nuevo_proceso->cantLiberar = 0;
				nuevo_proceso->cantOperacionesPriv = 0;
				nuevo_proceso->cantPaginasHeap = 0;
				nuevo_proceso->cantRafagas = 0;
				nuevo_proceso->cantSyscalls = 0;
				nuevo_proceso->codigo = codigo;

				nuevo_proceso->pcb.ERROR_NUM = 1;
				nuevo_proceso->pcb.PC = 0;
				nuevo_proceso->pcb.PID = PID_GLOBAL;
				t_metadata_program *info = metadata_desde_literal(codigo);
				nuevo_proceso->pcb.PC = info->instruccion_inicio;
				nuevo_proceso->pcb.cantidad_de_etiquetas = info->cantidad_de_etiquetas;
				nuevo_proceso->pcb.cantidad_de_funciones = info->cantidad_de_funciones;
				nuevo_proceso->pcb.etiquetas = info->etiquetas;
				nuevo_proceso->pcb.instrucciones_serializado = info->instrucciones_serializado;
				nuevo_proceso->pcb.instrucciones_size = info->instrucciones_size;

				queue_push(cola_NEW,nuevo_proceso);
				logearInfo("Petición de inicio de proceso [PID:%d]", PID_GLOBAL);
				intentar_iniciar_proceso();
			}
			else if (operacion == FINALIZAR_PROGRAMA) {
				int PID;
				recv(socketCliente, &PID, sizeof(PID), 0);
				eliminarProceso(PID);
				mem_finalizar_programa(PID);

				intentar_iniciar_proceso();
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
char* removerSaltoDeLinea(char* s) { // By Beej
    int len = strlen(s);

    if (len > 0 && s[len-1] == '\n')  // if there's a newline
        s[len-1] = '\0';          // truncate the string

    return s;
}
int soloNumeros(char *str) {
    while (*str) {
        if (isdigit(*str++) == 0) {
        	return 0;
        }
    }
    return 1;
}
int tipoCliente(int socketCliente) {
	DEF_MISMO_SOCKET(socketCliente);
	miCliente *found = (miCliente*)(list_find(clientes, mismoSocket));
	if (found == NULL) {
		return -1;
	}
	return found->identificador;
}
