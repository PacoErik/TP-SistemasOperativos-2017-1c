//-----HEADERS-----//
#include "op_memoria.h"
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include "commons/collections/list.h"
#include "commons/collections/queue.h"
#include <string.h>
#include "parser/metadata_program.h"
#include "commons/collections/list.h"

//-----DEFINES-----//
#define ID_CLIENTE(x) ID_CLIENTES[x-CONSOLA]
#define DEF_MISMO_SOCKET(SOCKET)										\
		_Bool mismoSocket(void* elemento) {							\
			return SOCKET == ((miCliente *) elemento)->socketCliente;	\
		}
enum Estado {NEW, READY, EXEC, BLOCKED, EXIT};

//-----ESTRUCTURAS-----//
typedef struct datosMemoria {
	int pid;
	short int codeSize;
	char *code;
} PACKED datosMemoria;

typedef struct miCliente {
    short socketCliente;
    char identificador;
    int en_uso;
} miCliente;

typedef t_list listaCliente;
typedef t_list listaProcesos;

typedef struct Posicion_memoria {
	int numero_pagina;
	int offset;
	int size;
} Posicion_memoria;

typedef struct Variable {
	char identificador;
	Posicion_memoria posicion;
} PACKED Variable;

typedef struct Entrada_stack {
	t_list *args;
	t_list *vars;
	int retPos;
	Posicion_memoria retVar;
} Entrada_stack;

typedef struct PCB {
	int pid;
	int program_counter;
	int cantidad_paginas_codigo;

	t_size cantidad_instrucciones;
	t_intructions *instrucciones_serializado;

	t_size etiquetas_size;
	char *etiquetas;

	int	puntero_stack;
	t_list *indice_stack;

	int exit_code;
} PCB;

//Un choclazo de struct para llevar la estadística de los procesos y tener
//un control más agrupado.. o algo así :v
typedef struct Proceso {
	int consola; //Socket de la consola que envio este proceso
	int cantidad_rafagas;
	int cantidad_operaciones_privilegiadas;
	int cantidad_paginas_heap;
	int cantidad_alocar;
	int bytes_alocados;
	int cantidad_liberar;
	int bytes_liberados;
	int cantidad_syscalls; //sí, me encantan los int
	char* codigo;
	int estado;
	PCB *pcb;
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

void 		agregar_cliente(char, int);
void 		agregar_proceso(Proceso *);
int			algoritmo_actual_es(char *);
miCliente*	algun_CPU_disponible();
void 		borrar_cliente(int);
int			cantidad_procesos(int);
int 		cantidad_procesos_sistema();
void 		cerrar_conexion(int, char*);
PCB*		deserializar_PCB(void*);
void		destruir_PCB(PCB*);
void		eliminar_proceso(int);
int 		enviar_mensaje_todos(int, char*);
void 		establecer_configuracion();
int 		existe_cliente(int);
int			existe_proceso(int);
void 		finalizar_programa(int);
void 		hacer_pedido_memoria(datosMemoria);
inline void imprimir_opciones_kernel();
void		inicializar_proceso(int, char *, Proceso *);
void 		intentar_iniciar_proceso();
void 		interaccion_kernel();
inline void limpiar_buffer_entrada();
void		planificar();
void 		procesar_mensaje(int, char, int);
int 		recibir_handshake(int);
int 		recibir_mensaje(int, int);
char*		remover_salto_linea(char*);
void*		serializar_PCB(PCB*, int*);
int			solo_numeros(char*);
void		terminar_kernel();
int 		tipo_cliente(int);

//-----PROCEDIMIENTO PRINCIPAL-----//
int main(void) {
	configurar("kernel");

	clientes = list_create();

	cola_NEW = queue_create();
	procesos = list_create();
	lista_EXIT = list_create();

	int servidor = socket(AF_INET, SOCK_STREAM, 0);

	if (servidor == -1) {
		logear_error("No se pudo crear el socket", true);
	}

	int activado = 1;
	setsockopt(servidor, SOL_SOCKET, SO_REUSEADDR, &activado, sizeof(activado));

	struct sockaddr_in servidor_info;

	servidor_info.sin_family = AF_INET;
	servidor_info.sin_port = htons(PUERTO_KERNEL);
	servidor_info.sin_addr.s_addr = INADDR_ANY;
	bzero(&(servidor_info.sin_zero), 8);

	if (bind(servidor, (struct sockaddr*) &servidor_info, sizeof(struct sockaddr)) == -1) {
		logear_error("Fallo al bindear el puerto", true);
	}

    if (listen(servidor, 10) == -1) {
		logear_error("Fallo al escuchar", true);
    }

    logear_info("Estoy escuchando");

    fd_set conectados;		// Set de FDs conectados
    fd_set read_fds;		// sockets de lectura

    FD_ZERO(&conectados);
    FD_ZERO(&read_fds);

    int fdmax;				// valor maximo de los FDs

    if (mem_conectar() == 0) {
    	logear_error("No se pudo conectar a la memoria.", true);
    }

	agregar_cliente(MEMORIA, socket_memoria);

	fdmax = (socket_memoria > servidor) ? socket_memoria : servidor; // Maximo entre fd de memoria y kernel

    FD_SET(servidor, &conectados);
    FD_SET(socket_memoria, &conectados);
    FD_SET(fileno(stdin), &conectados);

    imprimir_opciones_kernel();

    for(;;) {
        read_fds = conectados;

		if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
			logear_error("Error en el select", true);
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
					logear_error("Fallo en el accept", false);
				}
				else {
					FD_SET(nuevoCliente, &conectados);		// Agregarlo al set
					if (nuevoCliente > fdmax) {
						fdmax = nuevoCliente;				// Cambia el maximo
					}
					logear_info("Nueva conexión desde %s en el socket %d",
							inet_ntoa(cliente_info.sin_addr), nuevoCliente);
				}
			}
			// Mensaje por interfaz del Kernel
			else if (i == fileno(stdin)) {
				interaccion_kernel();
			}
			// Un cliente mando un mensaje
			else {
				if (existe_cliente(i) == 0) {			// Nuevo cliente, debe enviar un handshake
					if (recibir_handshake(i) == 0) {
						cerrar_conexion(i, "El socket %d se desconectó\n");
						FD_CLR(i, &conectados);
					}
					else {
						char *respuesta = "Bienvenido!";
						send(i, respuesta, strlen(respuesta) + 1, 0);
						if (tipo_cliente(i) == CPU)
							planificar();
					}
				}
				else {
					//Recibir header
					headerDeLosRipeados header;
					int bytesRecibidos = recibir_header(i, &header);
					if (bytesRecibidos <= 0) {
						int tipo = tipo_cliente(i);
						if (tipo >= 0) {
							logear_info("Cliente [%s] desconectado", ID_CLIENTE(tipo));
							borrar_cliente(i);
							close(i);
							if (tipo == MEMORIA)
								terminar_kernel();
						} else {
							cerrar_conexion(i, "El socket %d se desconectó\n");
						}
						FD_CLR(i, &conectados);
						continue;
					}

					int bytesDePayload = header.bytesDePayload;
					int codigoDeOperacion = header.codigoDeOperacion;

					//Procesar operación del header
					procesar_mensaje(i,codigoDeOperacion,bytesDePayload);
				}
			}
        }
    }
    list_destroy_and_destroy_elements(clientes, free);
    return 0;
}

//-----DEFINICIÓN DE FUNCIONES-----//

//MENSAJES
int enviar_mensaje_todos(int socketCliente, char* mensaje) {
	_Bool condicion(void* elemento) {
		miCliente *cliente = (miCliente*)elemento;
		return (cliente->identificador >= MEMORIA && cliente->identificador <= CPU);
	}

	listaCliente *clientes_filtrados = list_filter(clientes, condicion);

	void enviar_mensaje(void* elemento) {
		miCliente *cliente = (miCliente*)elemento;
		if (cliente->identificador == MEMORIA) {
			mem_mensaje(mensaje);
			return;
		}

		enviar_header(cliente->socketCliente, MENSAJE, strlen(mensaje));
		send(cliente->socketCliente, mensaje, strlen(mensaje), 0);

	}

	list_iterate(clientes_filtrados, enviar_mensaje);

	return list_size(clientes_filtrados);
}
void procesar_mensaje(int socketCliente, char operacion, int bytes) {

	int tipo = tipo_cliente(socketCliente);

	switch(tipo) {
		case CONSOLA:
			if (operacion == MENSAJE) {
				recibir_mensaje(socketCliente,bytes);
			}
			else if (operacion == INICIAR_PROGRAMA) {
				char* codigo = malloc(bytes+1);
				recv(socketCliente, codigo, bytes, 0);
				codigo[bytes]='\0';

				Proceso* nuevo_proceso = malloc(sizeof(Proceso));
				inicializar_proceso(socketCliente, codigo, nuevo_proceso);

				queue_push(cola_NEW,nuevo_proceso);
				logear_info("Petición de inicio de proceso [PID:%d]", PID_GLOBAL);
				PID_GLOBAL++;

				intentar_iniciar_proceso();
			}
			else if (operacion == FINALIZAR_PROGRAMA) {
				int PID;
				recv(socketCliente, &PID, sizeof(PID), 0);
				eliminar_proceso(PID);
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
			if (operacion == PCB_INCOMPLETO) {
				if (algoritmo_actual_es("FIFO")) {
					void *buffer_PCB = malloc(bytes);
					recv(socketCliente, buffer_PCB, bytes, 0);
					//se lo reenviamos para que complete hasta que se termine/bloquee
					enviar_header(socketCliente, PCB_INCOMPLETO, bytes);
					send(socketCliente, buffer_PCB, bytes, 0);
					free(buffer_PCB);

				}
			} else if (operacion == PCB_COMPLETO) {
				void *buffer_PCB = malloc(bytes);
				recv(socketCliente, buffer_PCB, bytes, 0);
				PCB *pcb = deserializar_PCB(buffer_PCB);
				free(buffer_PCB);

				//Buscamos el proceso correspondiente al PCB y le actualizamos
				//el PCB y de paso lo agregamos a la lista EXIT;
				_Bool mismoPID(void *param) {
					Proceso *proceso = (Proceso*) param;
					return proceso->pcb->pid == pcb->pid;
				}
				Proceso *proceso = list_remove_by_condition(procesos, &mismoPID);
				if (proceso != NULL) {
					destruir_PCB(proceso->pcb);
					proceso->pcb = pcb;
					list_add(lista_EXIT, proceso);

					enviar_header(proceso->consola, FINALIZAR_PROGRAMA, sizeof(pcb->pid));
					send(proceso->consola, &pcb->pid, sizeof(pcb->pid), 0);

					logear_info("[PID:%d] Proceso finalizado exitósamente!", pcb->pid);

					mem_finalizar_programa(pcb->pid);
				} else {
					logear_error("No existía el proceso con PID %d... weird", pcb->pid);
				}

				//Ya que completó el proceso, ponemos el CPU correspondiente
				//listo para un nuevo proceso
				_Bool mismoCPU(void *param) {
					miCliente *cpu = (miCliente*) param;
					return cpu->socketCliente == socketCliente;
				}
				miCliente *cpu = list_find(clientes, &mismoCPU);
				cpu->en_uso = false;
				logear_info("CPU con socket %d liberada.", cpu->socketCliente);


			}
			break;

		default:
			logear_error("Operación inválida de %s", true, ID_CLIENTE(tipo));
			break;
	}
}
int recibir_handshake(int socketCliente) {
	headerDeLosRipeados handy;

	recibir_header(socketCliente, &handy);

	if (CONSOLA <= handy.codigoDeOperacion
			&& handy.codigoDeOperacion <= CPU
			&& handy.bytesDePayload == 0) {
		logear_info("El nuevo cliente fue identificado como: %s", ID_CLIENTE(handy.codigoDeOperacion));
		agregar_cliente(handy.codigoDeOperacion, socketCliente);
		return 1;
	}
	return 0;
}
int recibir_mensaje(int socketCliente, int bytesDePayload) {
    char* mensaje = malloc(bytesDePayload+1);
    int bytesRecibidos = recv(socketCliente, mensaje, bytesDePayload, 0);
    mensaje[bytesDePayload]='\0';
    if (bytesRecibidos > 0) {
        logear_info("Mensaje recibido: %s", mensaje);
        int cantidad = enviar_mensaje_todos(socketCliente, mensaje);
        logear_info("Mensaje retransmitido a %i clientes", cantidad);
    } else {
    	cerrar_conexion(socketCliente,"Error al recibir mensaje del socket %i\n");
    }
    free(mensaje);
	return bytesRecibidos;
}

//INTERACCIÓN POR CONSOLA//
inline void imprimir_opciones_kernel() {
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
void interaccion_kernel() {
	struct comando {
		char *nombre;
		void (*funcion) (char *param);
	};

	void listado(char *estado) {
		logear_info("Comando de listado de programas ejecutado");
		string_trim(&estado);
		if (strlen(estado) == 0) {
			logear_info("[Listado] Todos:");
		} else if (strcmp("NEW",estado)==0) {
			logear_info("[Listado] NEW:");
		} else if (strcmp("READY",estado)==0) {
			logear_info("[Listado] READY:");
		} else if (strcmp("EXEC",estado)==0) {
			logear_info("[Listado] EXEC:");
		} else if (strcmp("BLOCKED",estado)==0) {
			logear_info("[Listado] BLOCKED:");
		} else if (strcmp("EXIT",estado)==0) {
			logear_info("[Listado] EXIT:");
		} else {
			logear_error("[Listado] Parámetro desconocido", false);
		}
		free(estado);
	}

	void proceso(char *param) {
		logear_info("[Proceso]");
	}

	void tablaglobal(char *param) {
		logear_info("[Tabla Global]");
	}

	void multiprogramacion(char *param) {
		logear_info("[Multiprogramación]");
	}

	void finalizar(char *sPID) {
		logear_info("Comando de desconexión de programa ejecutado");

		string_trim(&sPID);

		if (strlen(sPID) == 0) {
			logear_error("El comando \"finalizar\" recibe un parametro [PID]", false);
			free(sPID);
			return;
		}

		if (!solo_numeros(sPID)) {
			logear_error("Error: \"%s\" no es un PID valido!", false, sPID);
			free(sPID);
			return;
		}

		int PID = strtol(sPID, NULL, 0);
		free(sPID);

		logear_info("Proceso %d finalizado",PID);
	}

	void detener(char *param) {
		logear_info("[Detener]");
	}

	void planificar(char *param) {
		logear_info("[Planificar]");
	}

	void opciones(char *param) {
		string_trim(&param);
		if (strlen(param) != 0) {
			logear_error("El comando \"opciones\" no recibe nungun parametro", false);
			free(param);
			return;
		}
		free(param);
		imprimir_opciones_kernel();
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
		logear_error("Un comando no puede tener mas de 100 digitos", false);
		limpiar_buffer_entrada();
		return;
	}

	remover_salto_linea(input);

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
		logear_error("Error: %s no es un comando", false, cmd);
	}
}
inline void limpiar_buffer_entrada() {
	int c;
	while ((c = getchar()) != '\n' && c != EOF);
}

//MANEJO DE PROCESOS//
void agregar_proceso(Proceso *nuevo_proceso) {
	int pid = nuevo_proceso->pcb->pid;
	if (existe_proceso(pid)) {
		printf("Warning: Ya existe proceso con mismo PID\n");
	}
	list_add(procesos, nuevo_proceso);
}
int cantidad_procesos(int estado) {
	_Bool mismoEstado(void *param) {
		Proceso *proceso = (Proceso*) param;
		return proceso->estado == estado;
	}
	return list_count_satisfying(procesos, &mismoEstado);
}
int cantidad_procesos_sistema() {
	return list_size(procesos);
}
void eliminar_proceso(int PID) {
	_Bool mismoPID(void* elemento) {
		return PID == ((Proceso*) elemento)->pcb->pid;
	}

	Proceso* proceso = list_remove_by_condition(procesos,mismoPID);

	void borrar(void *param) {
		Entrada_stack *entrada = (Entrada_stack*) param;
		list_destroy_and_destroy_elements(entrada->args, free);
		list_destroy_and_destroy_elements(entrada->vars, free);
		free(entrada);
	}

	list_destroy_and_destroy_elements(proceso->pcb->indice_stack, &borrar);

	free(proceso->codigo);
	free(proceso->pcb->etiquetas);
	free(proceso->pcb->instrucciones_serializado);

	//Aún tiene memoria reservada para los otros campos.
	//Pero como dicen que hay que mantener la traza de ejecución
	//no los libero, ya que nos interesan saber las estadísticas de los
	//procesos finalizados.

	list_add(lista_EXIT,proceso);
}
int existe_proceso(int PID) {
	_Bool mismoPID(void* elemento) {
		return PID == ((Proceso*) elemento)->pcb->pid;
	}
	return list_any_satisfy(procesos, mismoPID);
}
void finalizar_programa(int PID) {
	if (existe_proceso(PID)) {
		eliminar_proceso(PID);
		logear_info("[PID:%d] Eliminado", PID);
	}
	else {
		logear_error("[PID:%d] No existe PID", false, PID);
	}
}
void hacer_pedido_memoria(datosMemoria datosMem) {
	int tamanioTotal = sizeof(int) + sizeof(datosMem.codeSize) + datosMem.codeSize;

	enviar_header(socket_memoria, INICIAR_PROGRAMA, tamanioTotal);

	char *buffer = malloc(tamanioTotal);				// Tamanio del codigo

	memcpy(buffer, &datosMem.pid, sizeof(int));		// Como no tengo puntero del pid (de code si), lo paso con &
	memcpy(buffer + sizeof(int), &datosMem.codeSize, sizeof(datosMem.codeSize)); // Aca termino de llenar el buffer que voy a mandar, copie pid primero y dsps codigo
	memcpy(buffer + sizeof(int) + sizeof(datosMem.codeSize), datosMem.code, datosMem.codeSize);

	send(socket_memoria, buffer, tamanioTotal, 0);

	free(buffer);
}
void intentar_iniciar_proceso() {
	if (cantidad_procesos_sistema() < GRADO_MULTIPROG) {
		Proceso* nuevo_proceso = queue_pop(cola_NEW);
		if (nuevo_proceso == NULL) {
			logear_info("No hay procesos en la cola NEW");
		}
		else {
			char *codigo = strdup(nuevo_proceso->codigo);
			int PID = nuevo_proceso->pcb->pid;

			int ret = mem_inicializar_programa(PID, strlen(codigo), codigo);
			if (ret == -1) {
				logear_error("Error de conexion con la memoria.", true);
			}

			printf("Pedido a memoria\n");
			free(codigo);

			if (ret == 0) {
				logear_error("[PID %d] Memoria insuficiente.", false, PID);
				nuevo_proceso->estado = EXIT;
				nuevo_proceso->pcb->exit_code = NO_SE_PUDIERON_RESERVAR_RECURSOS;
				list_add(lista_EXIT, nuevo_proceso);

				PID = -1;
				enviar_header(nuevo_proceso->consola, INICIAR_PROGRAMA, sizeof(PID));
				send(nuevo_proceso->consola, &PID, sizeof(PID), 0);
			}

			else {
				nuevo_proceso->estado = READY;
				agregar_proceso(nuevo_proceso);

				printf("Proceso agregado con PID: %d\n",PID);

				// Le envia el PID a la consola
				enviar_header(nuevo_proceso->consola, INICIAR_PROGRAMA, sizeof(PID));
				send(nuevo_proceso->consola, &PID, sizeof(PID), 0);

				planificar();
			}
		}
	}
}

//MANEJO DE CLIENTES//
void agregar_cliente(char identificador, int socketCliente) {
	if (existe_cliente(socketCliente)) {
		logear_error("No se puede agregar 2 veces mismo socket", false);
		return;
	}

	miCliente *cliente = malloc(sizeof (miCliente));
	cliente->identificador = identificador;
	cliente->socketCliente = socketCliente;
	cliente->en_uso = false;

	list_add(clientes, cliente);
}
void borrar_cliente(int socketCliente) {
	DEF_MISMO_SOCKET(socketCliente);
	list_remove_and_destroy_by_condition(clientes, mismoSocket, free);
}
void cerrar_conexion(int socketCliente, char* motivo) {
	logear_error(motivo, false, socketCliente);
	borrar_cliente(socketCliente);
	close(socketCliente);
}
int existe_cliente(int socketCliente) {
	DEF_MISMO_SOCKET(socketCliente);
	return list_any_satisfy(clientes, mismoSocket);
}
int tipo_cliente(int socketCliente) {
	DEF_MISMO_SOCKET(socketCliente);
	miCliente *found = (miCliente*)(list_find(clientes, mismoSocket));
	if (found == NULL) {
		return -1;
	}
	return found->identificador;
}

//PLANIFICACIÓN//
int algoritmo_actual_es(char *algoritmo) {
	return !strcmp(ALGORITMO,algoritmo);
}
miCliente *algun_CPU_disponible() {
	_Bool cpu_lista(void *param) {
		miCliente *cliente = (miCliente*) param;
		return cliente->identificador == CPU && !cliente->en_uso;
	}
	return list_find(clientes, &cpu_lista);
}
void inicializar_proceso(int socket, char *codigo, Proceso *nuevo_proceso) {
	nuevo_proceso->consola = socket;
	nuevo_proceso->bytes_alocados = 0;
	nuevo_proceso->bytes_liberados = 0;
	nuevo_proceso->cantidad_alocar = 0;
	nuevo_proceso->cantidad_liberar = 0;
	nuevo_proceso->cantidad_operaciones_privilegiadas = 0;
	nuevo_proceso->cantidad_paginas_heap = 0;
	nuevo_proceso->cantidad_rafagas = 0;
	nuevo_proceso->cantidad_syscalls = 0;
	nuevo_proceso->codigo = codigo;

	t_metadata_program *info = metadata_desde_literal(codigo);
	nuevo_proceso->pcb = malloc(sizeof(PCB));
	nuevo_proceso->pcb->exit_code = 1;
	nuevo_proceso->pcb->program_counter = info->instruccion_inicio;
	nuevo_proceso->pcb->pid = PID_GLOBAL;
	nuevo_proceso->pcb->etiquetas = info->etiquetas;
	nuevo_proceso->pcb->instrucciones_serializado = info->instrucciones_serializado;
	nuevo_proceso->pcb->cantidad_instrucciones = info->instrucciones_size;
	nuevo_proceso->pcb->etiquetas_size = info->etiquetas_size;
	nuevo_proceso->pcb->puntero_stack = 0;
	nuevo_proceso->pcb->cantidad_paginas_codigo = DIVIDE_ROUNDUP(strlen(codigo),tamanio_pagina);
	nuevo_proceso->pcb->indice_stack = list_create();

	//Entrada inicial del stack, no importa inicializar retPos y retVar
	//ya que es el contexto principal y no retorna nada
	Entrada_stack *entrada = malloc(sizeof(Entrada_stack));
	entrada->args = list_create();
	entrada->vars = list_create();
	list_add(nuevo_proceso->pcb->indice_stack,entrada);

	free(info);
}
void planificar() {
	miCliente *cpu = algun_CPU_disponible();
	//socket del CPU q se va a encargar de ejecutar
	//es NULL si no hay CPU disponible
	if (cpu != NULL) {
		if (cantidad_procesos(READY) > 0) {
			_Bool proceso_ready(void *param) {
				Proceso *proceso = (Proceso*) param;
				return proceso->estado == READY;
			}
			Proceso *proceso = list_remove_by_condition(procesos, &proceso_ready);
			proceso->estado = EXEC;
			cpu->en_uso = true;

			logear_info("Enviando PID %d a ejecución", proceso->pcb->pid);

			int buffersize;
			void *buffer = serializar_PCB(proceso->pcb, &buffersize);

			enviar_header(cpu->socketCliente, PCB_INCOMPLETO, buffersize);
			send(cpu->socketCliente, buffer, buffersize, 0);
			free(buffer);

			list_add(procesos, proceso);
		} else {
			logear_info("No hay procesos para planificar");
		}
	} else {
		logear_info("No hay CPUs disponibles");
	}
}

//SERIALIZADORES y DESERIALIZADORES//
void *list_serialize(t_list* list, int element_size, int *buffersize) {
	int element_count = list_size(list),offset = 8;
	*buffersize = offset + element_count*element_size;
	void *buffer = malloc(*buffersize);
	memcpy(buffer,&element_count,4);
	memcpy(buffer+4,&element_size,4);
	void copy(void *param) {
		memcpy(buffer+offset,param,element_size);
		offset += element_size;
	}
	list_iterate(list,&copy);
	return buffer;
}
t_list *list_deserialize(void *buffer) {
	t_list *new_list = list_create();
	int offset = 8,i,element_count,element_size;
	memcpy(&element_count,buffer,4);
	memcpy(&element_size,buffer+4,4);
	for (i=0;i<element_count;i++) {
		void *element = malloc(element_size);
		memcpy(element,buffer+offset,element_size);
		list_add(new_list,element);
		offset += element_size;
	}
	return new_list;
}
void *serializar_PCB(PCB *pcb, int* buffersize) {
	int instrucciones_size = pcb->cantidad_instrucciones * sizeof(t_intructions);
	int stack_size;
	void *stack_buffer = list_serialize(pcb->indice_stack,sizeof(Entrada_stack), &stack_size);
	void copy(void *param) {
		Entrada_stack *entrada = (Entrada_stack*) param;
		int args_size;
		int vars_size;
		void *args_buffer = list_serialize(entrada->args,sizeof(Posicion_memoria), &args_size);
		void *vars_buffer = list_serialize(entrada->vars,sizeof(Variable), &vars_size);
		stack_buffer = realloc(stack_buffer, stack_size + args_size + vars_size);
		memcpy(stack_buffer + stack_size, args_buffer, args_size);
		memcpy(stack_buffer + stack_size + args_size, vars_buffer, vars_size);
		stack_size += args_size + vars_size;
		free(args_buffer);
		free(vars_buffer);
	}
	list_iterate(pcb->indice_stack,&copy);
	*buffersize = sizeof(PCB) + instrucciones_size + pcb->etiquetas_size + stack_size;
	void *buffer = malloc(*buffersize);
	int offset = 0;
	memcpy(buffer+offset, pcb, sizeof(PCB));
	offset += sizeof(PCB);
	memcpy(buffer + offset, pcb->instrucciones_serializado, instrucciones_size);
	offset += instrucciones_size;
	memcpy(buffer + offset, pcb->etiquetas, pcb->etiquetas_size);
	offset += pcb->etiquetas_size;
	memcpy(buffer + offset, stack_buffer, stack_size);
	free(stack_buffer);

	return buffer;
}
PCB *deserializar_PCB(void *buffer) {
	PCB *pcb = malloc(sizeof(PCB));
	int offset = 0;
	memcpy(pcb, buffer, sizeof(PCB));
	offset += sizeof(PCB);
	int instrucciones_size = pcb->cantidad_instrucciones * sizeof(t_intructions);
	pcb->instrucciones_serializado = malloc(instrucciones_size);
	memcpy(pcb->instrucciones_serializado, buffer + offset, instrucciones_size);
	offset += instrucciones_size;
	pcb->etiquetas = malloc(pcb->etiquetas_size);
	memcpy(pcb->etiquetas, buffer + offset, pcb->etiquetas_size);
	offset += pcb->etiquetas_size;
	pcb->indice_stack = list_deserialize(buffer + offset);
	offset += list_size(pcb->indice_stack) * sizeof(Entrada_stack) + 8;
	void copy(void *param) {
		Entrada_stack *entrada = (Entrada_stack*) param;
		entrada->args = list_deserialize(buffer + offset);
		int args_size = list_size(entrada->args) * sizeof(Posicion_memoria) + 8;
		offset += args_size;
		entrada->vars = list_deserialize(buffer + offset);
		int vars_size = list_size(entrada->vars) * sizeof(Variable) + 8;
		offset += vars_size;
	}
	list_iterate(pcb->indice_stack,&copy);
	return pcb;
}

//EXTRA//
void establecer_configuracion() {
	PUERTO_KERNEL = config_get_int_value(config, "PUERTO_KERNEL");
	logear_info("Puerto Kernel: %d",PUERTO_KERNEL);

	strcpy(IP_MEMORIA,config_get_string_value(config, "IP_MEMORIA"));
	logear_info("IP Memoria: %s",IP_MEMORIA);

	PUERTO_MEMORIA = config_get_int_value(config, "PUERTO_MEMORIA");
	logear_info("Puerto Memoria: %d", PUERTO_MEMORIA);

	strcpy(IP_FS,config_get_string_value(config, "IP_FS"));
	logear_info("IP File System: %s",IP_FS);

	PUERTO_FS = config_get_int_value(config, "PUERTO_FS");
	logear_info("Puerto File System: %d", PUERTO_FS);

	QUANTUM = config_get_int_value(config, "QUANTUM");
	logear_info("QUANTUM: %d", QUANTUM);

	QUANTUM_SLEEP = config_get_int_value(config, "QUANTUM_SLEEP");
	logear_info("QUANTUM_SLEEP: %d", QUANTUM_SLEEP);

	strcpy(ALGORITMO,config_get_string_value(config, "ALGORITMO"));
	logear_info("ALGORITMO: %s",ALGORITMO);

	GRADO_MULTIPROG = config_get_int_value(config, "GRADO_MULTIPROG");
	logear_info("GRADO_MULTIPROG: %d", GRADO_MULTIPROG);

	STACK_SIZE = config_get_int_value(config, "STACK_SIZE");
	logear_info("STACK_SIZE: %d", STACK_SIZE);
}
char* remover_salto_linea(char* s) { // By Beej
    int len = strlen(s);

    if (len > 0 && s[len-1] == '\n')  // if there's a newline
        s[len-1] = '\0';          // truncate the string

    return s;
}
int solo_numeros(char *str) {
    while (*str) {
        if (isdigit(*str++) == 0) {
        	return 0;
        }
    }
    return 1;
}
void destruir_PCB(PCB *pcb) {
	void destruir_entrada_stack(void *param) {
		Entrada_stack *entrada = (Entrada_stack *) param;
		list_destroy_and_destroy_elements(entrada->vars, free);
		list_destroy_and_destroy_elements(entrada->args, free);
		free(entrada);
	}
	list_destroy_and_destroy_elements(pcb->indice_stack, &destruir_entrada_stack);
	free(pcb->etiquetas);
	free(pcb->instrucciones_serializado);
	free(pcb);
}
void terminar_kernel() {
	//Sucede cuando se desconecta la memoria
	//(proximamente el file system también)
	printf("Finalizando Kernel...\n");
	log_destroy(logger);
	list_destroy_and_destroy_elements(clientes, free);
	void borrar_proceso(void *param) {
		Proceso *proceso = (Proceso*) param;
		destruir_PCB(proceso->pcb);
		free(proceso->codigo);
		free(proceso);
	}
	list_destroy_and_destroy_elements(procesos, &borrar_proceso);
	queue_clean_and_destroy_elements(cola_NEW, &borrar_proceso);
	list_destroy_and_destroy_elements(lista_EXIT, free);
	printf("Adiós!\n");
	exit(0);
}
