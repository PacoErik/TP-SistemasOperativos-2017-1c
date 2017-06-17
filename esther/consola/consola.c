//-----HEADERS-----//
#include <pthread.h>
#include "commons/collections/list.h"
#include "commons/process.h"
#include <sys/types.h>
#include "qepd/qepd.h"
#include <string.h>
#include <signal.h>

//-----DEFINES-----//
#define DURACION(INICIO) ((double)(clock() - INICIO) / CLOCKS_PER_SEC)

//-----ESTRUCTURAS-----//
typedef struct proceso {
	int PID;
	pthread_t hiloID;
	time_t inicio;
	int cantidadImpresiones;
} proceso;
typedef t_list listaProceso;

//-----VARIABLES GLOBALES-----//
t_log* logger;
t_config* config;
int servidor;
char IP_KERNEL[16];
int PUERTO_KERNEL;
listaProceso *procesos;

//-----PROTOTIPOS DE FUNCIONES-----//
void 			agregar_proceso(int, pthread_t);
void 			configurar_programa();
void 			desconectar_consola();
void 			desconectar_programa();
void 			eliminar_proceso(int);
void 			enviar_header(int, char, int);
void 			enviar_mensaje(char*);
void 			establecer_configuracion();
pthread_t		hiloID_programa(int);
void 			imprimir_opciones_consola();
void*		 	iniciar_programa(void*);
void 			interaccion_consola();
void 			limpiar_buffer_entrada();
void 			limpiar_pantalla();
void 			manejar_signal_apagado(int);
void 			procesar_operacion(char, int);
void* 			recibir_headers(void*);
char*			remover_salto_linea(char*);
int				solo_numeros(char*);

//-----PROCEDIMIENTO PRINCIPAL-----//
int main(void) {

	signal(SIGINT, manejar_signal_apagado);
	signal(SIGTERM, manejar_signal_apagado);

	procesos = list_create();
	configurar("consola");
	conectar(&servidor, IP_KERNEL, PUERTO_KERNEL);
	handshake(servidor, CONSOLA);

	pthread_t hiloReceptor;
	pthread_create(&hiloReceptor, NULL, &recibir_headers, NULL);

	interaccion_consola();
	return 0;
}

//-----DEFINICIÓN DE FUNCIONES-----
void agregar_proceso(int PID, pthread_t hiloID) {
	proceso *nuevoProceso = malloc(sizeof(proceso));
	nuevoProceso->PID = PID;
	nuevoProceso->hiloID = hiloID;
	nuevoProceso->inicio = time(NULL); //tiempo actual en segundos
	nuevoProceso->cantidadImpresiones = 0;
	list_add(procesos, nuevoProceso);
}
void configurar_programa(char *ruta) {
	pthread_attr_t attr;
	pthread_t hiloPrograma;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&hiloPrograma, &attr, &iniciar_programa, ruta);
	pthread_attr_destroy(&attr);
}
void desconectar_consola() {
	logear_info("Se van a cerrar todos los procesos correspondientes a esta consola.");
	enviar_header(servidor, DESCONECTAR_CONSOLA, 0);

	list_destroy_and_destroy_elements(procesos, free);
	logear_info("Chau!");

	exit(0);
}
void desconectar_programa(int PID) {
	pthread_t TID = hiloID_programa(PID);
	if (TID == 0) {
		logear_error("No existe PID %d", false, PID);
		return;
	}
	logear_info("[PID:%d] Peticion de finalizacion enviada", PID);
	enviar_header(servidor, FINALIZAR_PROGRAMA, sizeof PID);
	send(servidor, &PID, sizeof PID, 0);

	pthread_cancel(TID);
}
void eliminar_proceso(int PID) {
	_Bool mismoPID(void* elemento) {
			return PID == ((proceso *) elemento)->PID;
		}
	proceso *proceso = list_remove_by_condition(procesos,mismoPID);

	//Estadística
	time_t inicio = proceso->inicio;
	time_t fin = time(NULL);
	char stringTiempo[20];
	strftime(stringTiempo, 20, "%d/%m (%H:%M)", localtime(&inicio));
	logear_info("[PID:%d] Inicio: %s", PID, stringTiempo);
	strftime(stringTiempo, 20, "%d/%m (%H:%M)", localtime(&fin));
	logear_info("[PID:%d] Fin: %s", PID, stringTiempo);
	logear_info("[PID:%d] Cantidad de impresiones: %d", PID, proceso->cantidadImpresiones);
	logear_info("[PID:%d] Duración: %.fs", PID, difftime(fin,inicio));
	//Fin estadística
	logear_info("[PID:%d] Finalizado", PID);
	free(proceso);
}
void enviar_mensaje(char *param) {
	char mensaje[512];
	memset(mensaje, 0, sizeof mensaje);

	if (strlen(param) == 0) {
		free(param);
		do {
			printf("\nEscribir mensaje: ");
			fgets(mensaje, sizeof mensaje, stdin);
			remover_salto_linea(mensaje);
			if (strlen(mensaje) == 0) {
				printf("Capo, hacé bien el mensaje"); // El mensaje no puede ser vacio
			}
		} while (strlen(mensaje) == 0);
	}
	else {
		strcpy(mensaje, param);
		free(param);
	}

	int bytes = strlen(mensaje);

	enviar_header(servidor, MENSAJE, bytes);
	send(servidor, mensaje, bytes, 0);
}
void establecer_configuracion() {
	if (config_has_property(config, "PUERTO_KERNEL")) {
		PUERTO_KERNEL = config_get_int_value(config, "PUERTO_KERNEL");
		logear_info("Puerto Kernel: %d", PUERTO_KERNEL);
	} else {
		logear_error("Error al leer el puerto del Kernel", true);
	}
	if (config_has_property(config, "IP_KERNEL")) {
		strcpy(IP_KERNEL, config_get_string_value(config, "IP_KERNEL"));
		logear_info("IP Kernel: %s", IP_KERNEL);
	} else {
		logear_error("Error al leer la IP del Kernel", true);
	}
}
pthread_t hiloID_programa(int PID) {
	_Bool mismoPID(void* elemento) {
		return PID == ((proceso *) elemento)->PID;
	}
	proceso *elemento = list_find(procesos, mismoPID);
	if (elemento == NULL) {
		return 0;
	}
	return elemento->hiloID;
}
inline void imprimir_opciones_consola() {
	printf(
			"\n--------------------\n"
			"BIENVENIDO A LA CONSOLA\n"
			"Lista de comandos: \n"
			"iniciar [RUTA] "
				"\t/Iniciar programa AnSISOP\n"
			"finalizar [PID] "
				"\t//Finalizar programa AnSISOP\n"
			"salir "
				"\t//Desconectar consola\n"
			"mensaje "
				"\t//Enviar mensaje\n"
			"mensaje [MENSAJE] "
				"\t//Enviar mensaje\n"
			"limpiar "
				"\t//Limpiar mensajes\n"
			"opciones "
				"\t//Mostrar opciones\n"
	);
}
void* iniciar_programa(void* arg) {
	//Inicio del programa
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	pthread_t id_hilo = pthread_self();

	//Chequeo de que el archivo del programa ingresado exista
	char* ruta = arg;
	logear_info("Ruta ingresada: %s",ruta);
	if (!existe_archivo(ruta)) {
		logear_error("No se encontró el archivo %s",false,ruta);
		return NULL;
	} else if (!string_ends_with(ruta,".ansisop")) {
		logear_error("El archivo %s no es un programa válido",false,ruta);
		return NULL;
	}

	//Lectura y envío del programa
	FILE* prog = fopen(ruta,"r");

	char* codigo = NULL;
	long int bytes;

	fseek(prog, 0, SEEK_END);
	bytes = ftell(prog);
	fseek(prog, 0, SEEK_SET);
	codigo = malloc(bytes);
	fread(codigo, 1, bytes, prog);
	fclose(prog);

	int PID = -1;

	if (bytes > 0) {
		enviar_header(servidor, INICIAR_PROGRAMA, bytes);
		agregar_proceso(PID,id_hilo);
		logear_info("[Programa] Petición de inicio de %s enviada",ruta);
		send(servidor, codigo, bytes, 0);
	} else if (bytes == 0) {
		logear_error("Archivo vacio: %s", false, ruta);
		free(arg);
		free(codigo); // Hay que ver si codigo no es NULL cuando no se leyo nada
		return NULL;
	} else {
		logear_error("No se pudo leer el archivo: %s", false, ruta);
		free(arg);
		return NULL;
	}

	free(arg); //ya no necesitamos más la ruta
	free(codigo); //ya no necesitamos más el código

	for(;;);
	printf("Nunca me ejecutarán :CCC");
	return NULL;
}
void interaccion_consola() {
	struct comando {
		char *nombre;
		void (*funcion) (char *param);
	};

	void iniciar(char *ruta) {
		logear_info("Comando de inicio de programa ejecutado");
		string_trim(&ruta);

		if (strlen(ruta) == 0) {
			logear_error("El comando \"iniciar\" recibe un parametro [RUTA]", false);
			free(ruta);
			return;
		}

		configurar_programa(ruta);
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

		desconectar_programa(PID);
	}

	void salir(char *param) {
		logear_info("Comando de apagado de consola ejecutado");
		string_trim(&param);
		if (strlen(param) != 0) {
			logear_error("El comando \"desconectar\" no recibe ningun parametro", false);
			free(param);
			return;
		}
		free(param);
		desconectar_consola();
	}

	void mensaje(char *param) {
		logear_info("Comando de envío de mensaje ejecutado");
		enviar_mensaje(param);
	}

	void limpiar(char *param) {
		string_trim(&param);
		if (strlen(param) != 0) {
			logear_error("El comando \"limpiar\" no recibe nungun parametro", false);
			free(param);
			return;
		}
		free(param);
		limpiar_pantalla();
	}

	void opciones(char *param) {
		string_trim(&param);
		if (strlen(param) != 0) {
			logear_error("El comando \"opciones\" no recibe parámetros", false);
			free(param);
			return;
		}
		free(param);
		imprimir_opciones_consola();
	}

	struct comando comandos[] = {
		{ "iniciar", iniciar },
		{ "finalizar", finalizar },
		{ "salir", salir },
		{ "mensaje", mensaje },
		{ "limpiar", limpiar },
		{ "opciones", opciones }
	};

	imprimir_opciones_consola();

	char input[100];
	while (1) {
		memset(input, 0, sizeof input);
		fgets(input, sizeof input, stdin);

		if (strlen(input) == 1) {
			continue;
		}

		if (input[strlen(input) - 1] != '\n') {
			logear_error("Un comando no puede tener mas de 100 caracteres", false);
			limpiar_buffer_entrada();
			continue;
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
}
inline void limpiar_buffer_entrada() {
	int c;
	while ((c = getchar()) != '\n' && c != EOF);
}
void limpiar_pantalla() {
	printf("\033[H\033[J");
}
void manejar_signal_apagado(int sig) {
   desconectar_consola();
}
void procesar_operacion(char operacion, int bytes) {
	int PID;
	switch (operacion) {
		case MENSAJE: ;
			char* mensaje = malloc(bytes+1);
			recv(servidor, mensaje, bytes, 0);
			mensaje[bytes] = '\0';
			logear_info("Mensaje recibido: %s", mensaje);
			free(mensaje);
			break;
		case INICIAR_PROGRAMA:
			recv(servidor, &PID, sizeof(PID), 0);

			_Bool esNuevo(void* elemento) {
				return -1 == ((proceso *) elemento)->PID;
			}

			//Actualizamos el PID del proceso que habíamos agregado
			//con PID = -1;
			proceso *procesoAux = list_find(procesos,esNuevo);
			procesoAux->PID = PID;

			if (PID != -1) {
				logear_info("[PID:%d] Programa iniciado",PID);
			} else {
				logear_info("No se pudo iniciar programa por falta de recursos");
				eliminar_proceso(PID);
			}
			break;
		case FINALIZAR_PROGRAMA:
			recv(servidor, &PID, sizeof(PID), 0);
			eliminar_proceso(PID);
			break;
		case IMPRIMIR:;
			char *informacion = malloc(bytes);
			recv(servidor, informacion, bytes, 0);
			recv(servidor, &PID, sizeof(PID), 0);

			_Bool mismo_proceso(void *param) {
				proceso *proceso_auxiliar = param;
				return proceso_auxiliar->PID == PID;
			}

			proceso *un_proceso = list_find(procesos, &mismo_proceso);

			un_proceso->cantidadImpresiones++;

			logear_info("[PID:%d] Imprimir: %s", PID, informacion);
			break;
		default:
			logear_error("Operación inválida", false);
			break;
	}
}
void* recibir_headers(void* arg) {
	while (1) {
		headerDeLosRipeados header;

		int bytesRecibidos = recibir_header(servidor, &header);

		if (bytesRecibidos <= 0) {
			logear_error("Se desconectó el Kernel",true);
		}

		procesar_operacion(header.codigoDeOperacion,header.bytesDePayload);
	}
	return NULL;
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
