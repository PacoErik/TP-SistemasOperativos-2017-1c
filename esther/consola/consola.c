//-----HEADERS-----//
#include <pthread.h>
#include "commons/collections/list.h"
#include "commons/process.h"
#include <sys/types.h>
#include "qepd/qepd.h"
#include <string.h>
#include <signal.h>
#include <semaphore.h>

//-----DEFINES-----//
#define DURACION(INICIO) ((double)(clock() - INICIO) / CLOCKS_PER_SEC)

//-----ESTRUCTURAS-----//
typedef struct proceso {
	int PID;
	pthread_t hiloID;
	time_t inicio;
	int cantidad_impresiones;
	char *ruta;
	sem_t mensaje_a_procesar;
	char operacion;
	unsigned short bytes_a_recibir;
	_Bool falta_de_recursos;
} info_hilo;
typedef t_list listaProceso;

//-----VARIABLES GLOBALES-----//
t_log* logger;
t_config* config;
int kernel_socket;
char IP_KERNEL[16];
int PUERTO_KERNEL;
listaProceso *hilos_programa;
sem_t mensaje_resuelto;
pthread_t hilo_receptor;

//-----PROTOTIPOS DE FUNCIONES-----//
info_hilo*		agregar_info_hilo(int, pthread_t, char*);
void 			configurar_programa();
void 			desconectar_hilos();
void 			desconectar_programa();
void 			eliminar_info_hilo(int);
void 			enviar_header(int, char, int);
void 			establecer_configuracion();
pthread_t		hiloID_programa(int);
void 			imprimir_opciones_consola();
info_hilo*		info_hilo_segun_PID(int);
void*		 	manejar_programa(void*);
void 			interaccion_consola();
void 			limpiar_buffer_entrada();
void 			limpiar_pantalla();
void 			manejar_signal_apagado(int);
void 			procesar_operacion(char, int);
void* 			recibir_headers(void*);
char*			remover_salto_linea(char*);
int				solo_numeros(char*);
void			terminar_consola();

//-----PROCEDIMIENTO PRINCIPAL-----//
int main(void) {
	sem_init(&mensaje_resuelto, 0, 0);

	signal(SIGINT, manejar_signal_apagado);
	signal(SIGTERM, manejar_signal_apagado);

	hilos_programa = list_create();
	configurar("consola");
	conectar(&kernel_socket, IP_KERNEL, PUERTO_KERNEL);
	handshake(kernel_socket, CONSOLA);

	pthread_create(&hilo_receptor, NULL, &recibir_headers, NULL);

	interaccion_consola();
	return 0;
}

//-----DEFINICIÓN DE FUNCIONES-----
info_hilo *agregar_info_hilo(int PID, pthread_t hiloID, char *ruta) {
	info_hilo *nueva_info_hilo = malloc(sizeof(info_hilo));
	nueva_info_hilo->PID = PID;
	nueva_info_hilo->hiloID = hiloID;
	nueva_info_hilo->ruta = ruta;
	nueva_info_hilo->falta_de_recursos = false;
	sem_init(&nueva_info_hilo->mensaje_a_procesar, 0, 0);
	list_add(hilos_programa, nueva_info_hilo);
	return nueva_info_hilo;
}
void configurar_programa(char *ruta) {
	pthread_attr_t attr;
	pthread_t hiloPrograma;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&hiloPrograma, &attr, &manejar_programa, ruta);
	pthread_attr_destroy(&attr);
}
void desconectar_hilos() {
	if (list_size(hilos_programa) > 0) {
		logear_info("[Desconexión] Cerrando hilos...");
	} else {
		logear_info("[Desconexión] No hay hilos para cerrar!");
	}

	enviar_header(kernel_socket, DESCONECTAR_CONSOLA, 0);

	while (list_size(hilos_programa) > 0) {
		info_hilo *info = list_get(hilos_programa, 0);
		info->operacion = FINALIZAR_PROGRAMA;
		sem_post(&info->mensaje_a_procesar);
		sem_wait(&mensaje_resuelto);
	}
}
void desconectar_programa(int PID) {
	info_hilo *info = info_hilo_segun_PID(PID);

	if (info == NULL || PID < 0) {
		logear_error("No existe PID %d", false, PID);
		return;
	}

	logear_info("[PID:%d] Pedido de finalización enviada", PID);
	info->operacion = FINALIZAR_PROGRAMA;
	sem_post(&info->mensaje_a_procesar);
	sem_wait(&mensaje_resuelto);

	enviar_header(kernel_socket, FINALIZAR_PROGRAMA, sizeof PID);
	send(kernel_socket, &PID, sizeof PID, 0);

}
void eliminar_info_hilo(int PID) {
	_Bool mismo_PID(void* elemento) {
			return PID == ((info_hilo *) elemento)->PID;
		}
	info_hilo *info = list_remove_by_condition(hilos_programa,mismo_PID);

	if (PID < 0) {
		if (info->falta_de_recursos) {
			logear_info("El programa (%s) finalizó debido a falta de recursos", info->ruta);
		} else {
			logear_info("El programa (%s) finalizó debido a desconexión de hilo", info->ruta);
		}
	} else {
		//Estadística
		time_t inicio = info->inicio;
		time_t fin = time(NULL);
		char string_tiempo[20];
		strftime(string_tiempo, 20, "%d/%m (%H:%M)", localtime(&inicio));
		logear_info("[PID:%d] Finalización del programa (%s)", PID, info->ruta);
		logear_info("[PID:%d] Inicio: %s", PID, string_tiempo);
		strftime(string_tiempo, 20, "%d/%m (%H:%M)", localtime(&fin));
		logear_info("[PID:%d] Fin: %s", PID, string_tiempo);
		logear_info("[PID:%d] Cantidad de impresiones: %d", PID, info->cantidad_impresiones);
		logear_info("[PID:%d] Duración: %.fs", PID, difftime(fin,inicio));
		//Fin estadística
		logear_info("[PID:%d] Finalizado", PID);
	}
	sem_destroy(&info->mensaje_a_procesar);
	free(info->ruta);
	free(info);
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
	_Bool mismo_PID(void* elemento) {
		return PID == ((info_hilo *) elemento)->PID;
	}
	info_hilo *elemento = list_find(hilos_programa, mismo_PID);
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
			"iniciar [Ruta] "
				"\t\t\t//Iniciar programa AnSISOP\n"
			"finalizar [Número de PID] "
				"\t//Finalizar programa AnSISOP\n"
			"desconectar "
				"\t\t\t//Desconectar hilos\n"
			"limpiar "
				"\t\t\t//Limpiar mensajes\n"
			"opciones "
				"\t\t\t//Mostrar comandos\n"
	);
}
info_hilo *info_hilo_segun_PID(int PID) {
	_Bool mismo_PID(void *p) {
		return PID == ((info_hilo*)p)->PID;
	}
	return list_find(hilos_programa, &mismo_PID);
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

	void desconectar(char *param) {
		logear_info("Comando de terminación de hilos ejecutado");
		string_trim(&param);
		if (strlen(param) != 0) {
			logear_error("El comando \"desconectar\" no recibe ningún parámetro", false);
			free(param);
			return;
		}
		free(param);
		desconectar_hilos();
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
		{ "desconectar", desconectar },
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

		free(inputline);
	}
}
inline void limpiar_buffer_entrada() {
	int c;
	while ((c = getchar()) != '\n' && c != EOF);
}
void limpiar_pantalla() {
	printf("\033[H\033[J");
}
void* manejar_programa(void* arg) {
	//Inicio del programa
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	pthread_t id_hilo = pthread_self();

	//Chequeo de que el archivo del programa ingresado exista
	char* ruta = arg;
	logear_info("Ruta ingresada: %s",ruta);
	if (!existe_archivo(ruta)) {
		logear_error("No se encontró el archivo %s", false, ruta);
		free(arg);
		return NULL;
	}
	else if (!string_ends_with(ruta, ".ansisop")) {
		logear_error("El archivo %s no es un programa válido", false, ruta);
		free(arg);
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
		enviar_header(kernel_socket, INICIAR_PROGRAMA, bytes);
		info_hilo *info = agregar_info_hilo(PID, id_hilo, ruta);
		logear_info("[Programa] Petición de inicio de %s enviada", ruta);
		send(kernel_socket, codigo, bytes, 0);
		free(codigo);
		while (true) {
			sem_wait(&info->mensaje_a_procesar);

			switch (info->operacion) {

			case INICIAR_PROGRAMA:
				info->inicio = time(NULL);
				info->cantidad_impresiones = 0;
				logear_info("[PID:%d] Programa iniciado (%s)", info->PID, info->ruta);
				break;

			case FINALIZAR_PROGRAMA:
				eliminar_info_hilo(info->PID);
				sem_post(&mensaje_resuelto);
				pthread_exit(NULL);
				break;

			case IMPRIMIR:;
				char *informacion = malloc(info->bytes_a_recibir);
				recv(kernel_socket, informacion, info->bytes_a_recibir, 0);

				info->cantidad_impresiones++;

				logear_info("[PID:%d] Imprimir: %s", info->PID, informacion);
				free(informacion);

				break;
			}

			sem_post(&mensaje_resuelto);
		}

	} else if (bytes == 0) {
		logear_error("Archivo vacio: %s", false, ruta);
		free(arg);
		free(codigo);
		return NULL;
	} else {
		logear_error("No se pudo leer el archivo: %s", false, ruta);
		free(arg);
		return NULL;
	}
}
void manejar_signal_apagado(int sig) {
   terminar_consola();
}
void procesar_operacion(char operacion, int bytes) {
	int PID;

	if (operacion == FALLO_INICIO_PROGRAMA) {
		info_hilo *info = info_hilo_segun_PID(-1);
		if (info == NULL) return;

		info->operacion = FINALIZAR_PROGRAMA;
		info->falta_de_recursos = true;
		sem_post(&info->mensaje_a_procesar);
		sem_wait(&mensaje_resuelto);

		return;
	}

	if (operacion == INICIAR_PROGRAMA) {
		recv(kernel_socket, &PID, sizeof(PID), 0);

		info_hilo *info = info_hilo_segun_PID(-1);
		if (info == NULL) return;

		info->PID = PID;
		info->operacion = operacion;
		sem_post(&info->mensaje_a_procesar);
		sem_wait(&mensaje_resuelto);

		return;
	}

	if (operacion == FINALIZAR_PROGRAMA || operacion == IMPRIMIR) {

		recv(kernel_socket, &PID, sizeof(PID), 0);

		info_hilo *info = info_hilo_segun_PID(PID);
		if (info == NULL) return;

		info->bytes_a_recibir = bytes;
		info->operacion = operacion;
		sem_post(&info->mensaje_a_procesar);
		sem_wait(&mensaje_resuelto);

		return;
	}

	logear_error("Operación inválida", false);
}
void* recibir_headers(void* arg) {
	while (1) {
		headerDeLosRipeados header;

		int bytes_recibidos = recibir_header(kernel_socket, &header);

		if (bytes_recibidos <= 0) {
			void _borrar(void *p) {
				info_hilo *info = p;
				sem_destroy(&info->mensaje_a_procesar);
				free(info->ruta);
				pthread_cancel(info->hiloID);
				free(info);
			}
			sem_destroy(&mensaje_resuelto);
			list_destroy_and_destroy_elements(hilos_programa, &_borrar);
			logear_error("Se desconectó el Kernel", false);
			log_destroy(logger);
			pthread_cancel(hilo_receptor);
			exit(0);
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
void terminar_consola() {
	desconectar_hilos();
	list_destroy(hilos_programa);
	sem_destroy(&mensaje_resuelto);
	logear_info("Chau!");
	log_destroy(logger);
	pthread_cancel(hilo_receptor);
	exit(0);
}
