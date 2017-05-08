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
void 			agregarProceso(int, pthread_t, time_t);
void 			configurarPrograma();
void 			desconectarConsola();
void 			desconectarPrograma();
void 			eliminarProceso(int);
void 			enviarHeader(int, char, int);
void 			enviarMensaje(char*);
void 			establecerConfiguracion();
pthread_t		hiloIDPrograma(int);
void 			imprimirOpcionesDeConsola();
void*		 	iniciarPrograma(void*);
void 			interaccionConsola();
void 			limpiarBufferEntrada();
void 			limpiarPantalla();
void 			manejarSignalApagado(int);
void 			procesarOperacion(char, int);
void* 			recibirHeaders(void*);
char*			removerSaltoDeLinea(char*);
int				soloNumeros(char*);

//-----PROCEDIMIENTO PRINCIPAL-----//
int main(void) {

	signal(SIGINT, manejarSignalApagado);
	signal(SIGTERM, manejarSignalApagado);

	procesos = list_create();
	configurar("consola");
	conectar(&servidor, IP_KERNEL, PUERTO_KERNEL);
	handshake(servidor, CONSOLA);

	pthread_t hiloReceptor;
	pthread_create(&hiloReceptor, NULL, &recibirHeaders, NULL);

	interaccionConsola();
	return 0;
}

//-----DEFINICIÓN DE FUNCIONES-----
void agregarProceso(int PID, pthread_t hiloID, time_t inicio) {
	proceso *nuevoProceso = malloc(sizeof(proceso));
	nuevoProceso->PID = PID;
	nuevoProceso->hiloID = hiloID;
	nuevoProceso->inicio = inicio;
	nuevoProceso->cantidadImpresiones = 0;
	list_add(procesos, nuevoProceso);
}
void configurarPrograma(char *ruta) {
	pthread_attr_t attr;
	pthread_t hiloPrograma;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&hiloPrograma, &attr, &iniciarPrograma, ruta);
	pthread_attr_destroy(&attr);
}
void desconectarConsola() {
	void _finalizar(void *element) {
		proceso *proc = (proceso*) element;
		int PID = proc->PID;
		desconectarPrograma(PID);
	}
	list_destroy_and_destroy_elements(procesos, _finalizar);
	close(servidor);

	logearInfo("Chau!");

	exit(0);
}
void desconectarPrograma(int PID) {
	logearInfo("[PID:%d] Finalizando...", PID);
	pthread_t TID = hiloIDPrograma(PID);
	if (TID == 0) {
		logearError("No existe PID %d", false, PID);
		return;
	}
	enviarHeader(servidor, FINALIZAR_PROGRAMA, sizeof PID);
	send(servidor, &PID, sizeof PID, 0);

	pthread_cancel(TID);

	_Bool mismoPID(void* elemento) {
			return PID == ((proceso *) elemento)->PID;
		}
	proceso *procesoAux = list_find(procesos,mismoPID);

	//Estadística
	time_t inicio = procesoAux->inicio;
	time_t fin = time(NULL);
	char stringTiempo[20];
	strftime(stringTiempo, 20, "%d/%m (%H:%M)", localtime(&inicio));
	logearInfo("[PID:%d] Inicio: %s", PID, stringTiempo);
	strftime(stringTiempo, 20, "%d/%m (%H:%M)", localtime(&fin));
	logearInfo("[PID:%d] Fin: %s", PID, stringTiempo);
	logearInfo("[PID:%d] Cantidad de impresiones: %d", PID, procesoAux->cantidadImpresiones);
	logearInfo("[PID:%d] Duración: %.fs", PID, difftime(fin,inicio));
	//Fin estadística

	eliminarProceso(PID);

	logearInfo("[PID:%d] Proceso finalizado", PID);
}
void eliminarProceso(int PID) {
	_Bool mismoPID(void* elemento) {
		return PID == ((proceso *) elemento)->PID;
	}
	list_remove_and_destroy_by_condition(procesos, mismoPID, free);
}
void enviarHeader(int socket, char operacion, int bytes) {
	headerDeLosRipeados headerDeMiMensaje;
	headerDeMiMensaje.bytesDePayload = bytes;
	headerDeMiMensaje.codigoDeOperacion = operacion;

	int headerSize = sizeof(headerDeMiMensaje);
	void *headerComprimido = malloc(headerSize);
	serializarHeader(&headerDeMiMensaje, headerComprimido);

	send(socket, headerComprimido, headerSize, 0);
	free(headerComprimido);
}
void enviarMensaje(char *param) {
	char mensaje[512];
	memset(mensaje, 0, sizeof mensaje);

	if (strlen(param) == 0) {
		free(param);
		do {
			printf("\nEscribir mensaje: ");
			fgets(mensaje, sizeof mensaje, stdin);
			removerSaltoDeLinea(mensaje);
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

	enviarHeader(servidor, MENSAJE, bytes);
	send(servidor, mensaje, bytes, 0);
}
void establecerConfiguracion() {
	if (config_has_property(config, "PUERTO_KERNEL")) {
		PUERTO_KERNEL = config_get_int_value(config, "PUERTO_KERNEL");
		logearInfo("Puerto Kernel: %d", PUERTO_KERNEL);
	} else {
		logearError("Error al leer el puerto del Kernel", true);
	}
	if (config_has_property(config, "IP_KERNEL")) {
		strcpy(IP_KERNEL, config_get_string_value(config, "IP_KERNEL"));
		logearInfo("IP Kernel: %s", IP_KERNEL);
	} else {
		logearError("Error al leer la IP del Kernel", true);
	}
}
pthread_t hiloIDPrograma(int PID) {
	_Bool mismoPID(void* elemento) {
		return PID == ((proceso *) elemento)->PID;
	}
	proceso *elemento = list_find(procesos, mismoPID);
	if (elemento == NULL) {
		return 0;
	}
	return elemento->hiloID;
}
inline void imprimirOpcionesDeConsola() {
	printf(
			"\n--------------------\n"
			"\n"
			"BIENVENIDO A LA CONSOLA\n\n"
			"Lista de comandos: \n\n"
			"iniciar [RUTA]\n"
				"\tIniciar programa AnSISOP\n\n"
			"finalizar [PID]\n"
				"\tFinalizar programa AnSISOP\n\n"
			"salir\n"
				"\tDesconectar consola\n\n"
			"mensaje\n"
				"\tEnviar mensaje\n\n"
			"mensaje [MENSAJE]\n"
				"\tEnviar mensaje\n\n"
			"limpiar\n"
				"\tLimpiar mensajes\n\n"
			"opciones\n"
				"\tMostrar opciones\n"
	);
}
void* iniciarPrograma(void* arg) {
	//Inicio del programa
	time_t inicio = time(NULL); //Obtiene el tiempo actual
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	pthread_t id_hilo = pthread_self();

	//Chequeo de que el archivo del programa ingresado exista
	char* ruta = arg;
	logearInfo("Ruta ingresada: %s",ruta);
	if (!existeArchivo(ruta)) {
		logearError("No se encontró el archivo %s",false,ruta);
		return NULL;
	} else if (!string_ends_with(ruta,".ansisop")) {
		logearError("El archivo %s no es un programa válido",false,ruta);
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
		enviarHeader(servidor, INICIAR_PROGRAMA, bytes);
		agregarProceso(PID,id_hilo,inicio);
		logearInfo("[Programa] Petición de inicio de %s enviada",ruta);
		send(servidor, codigo, bytes, 0);
	} else if (bytes == 0) {
		logearError("Archivo vacio: %s", false, ruta);
		free(arg);
		free(codigo); // Hay que ver si codigo no es NULL cuando no se leyo nada
		return NULL;
	} else {
		logearError("No se pudo leer el archivo: %s", false, ruta);
		free(arg);
		return NULL;
	}

	free(arg); //ya no necesitamos más la ruta
	free(codigo); //ya no necesitamos más el código

	for(;;);
	printf("Nunca me ejecutarán :CCC");
	return NULL;
}
void interaccionConsola() {
	struct comando {
		char *nombre;
		void (*funcion) (char *param);
	};

	void iniciar(char *ruta) {
		logearInfo("Comando de inicio de programa ejecutado");
		string_trim(&ruta);

		if (strlen(ruta) == 0) {
			logearError("El comando \"iniciar\" recibe un parametro [RUTA]", false);
			free(ruta);
			return;
		}

		configurarPrograma(ruta);
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

		desconectarPrograma(PID);
	}

	void salir(char *param) {
		logearInfo("Comando de apagado de consola ejecutado");
		string_trim(&param);
		if (strlen(param) != 0) {
			logearError("El comando \"desconectar\" no recibe nungun parametro", false);
			free(param);
			return;
		}
		free(param);
		desconectarConsola();
	}

	void mensaje(char *param) {
		logearInfo("Comando de envío de mensaje ejecutado");
		enviarMensaje(param);
	}

	void limpiar(char *param) {
		string_trim(&param);
		if (strlen(param) != 0) {
			logearError("El comando \"limpiar\" no recibe nungun parametro", false);
			free(param);
			return;
		}
		free(param);
		limpiarPantalla();
	}

	void opciones(char *param) {
		string_trim(&param);
		if (strlen(param) != 0) {
			logearError("El comando \"opciones\" no recibe nungun parametro", false);
			free(param);
			return;
		}
		free(param);
		imprimirOpcionesDeConsola();
	}

	struct comando comandos[] = {
		{ "iniciar", iniciar },
		{ "finalizar", finalizar },
		{ "salir", salir },
		{ "mensaje", mensaje },
		{ "limpiar", limpiar },
		{ "opciones", opciones }
	};

	imprimirOpcionesDeConsola();

	char input[100];
	while (1) {
		memset(input, 0, sizeof input);
		fgets(input, sizeof input, stdin);

		if (strlen(input) == 1) {
			continue;
		}

		if (input[strlen(input) - 1] != '\n') {
			logearError("Un comando no puede tener mas de 100 digitos", false);
			limpiarBufferEntrada();
			continue;
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
}
inline void limpiarBufferEntrada() {
	int c;
	while ((c = getchar()) != '\n' && c != EOF);
}
void limpiarPantalla() {
	printf("\033[H\033[J");
}
void manejarSignalApagado(int sig) {
   desconectarConsola();
}
void procesarOperacion(char operacion, int bytes) {
	switch (operacion) {
		case MENSAJE: ;
			char* mensaje = malloc(bytes+1);
			recv(servidor, mensaje, bytes, 0);
			mensaje[bytes] = '\0';
			logearInfo("Mensaje recibido: %s", mensaje);
			free(mensaje);
			break;
		case INICIAR_PROGRAMA: ;
			int PID;
			recv(servidor, &PID, sizeof(PID), 0);

			_Bool esNuevo(void* elemento) {
				return -1 == ((proceso *) elemento)->PID;
			}

			//Actualizamos el PID del proceso que habíamos agregado
			//con PID = -1;
			proceso *procesoAux = list_find(procesos,esNuevo);
			procesoAux->PID = PID;

			logearInfo("[PID:%d] Programa iniciado",PID);
			break;
		case ERROR_MULTIPROGRAMACION:
			logearInfo("No se pudo crear el programa");
			//Borramos el proceso que habíamos creado y seteado
			//con PID = -1
			eliminarProceso(-1);
			break;
		default:
			logearError("Operación inválida", false);
			break;
	}
}
void* recibirHeaders(void* arg) {
	while (1) {
		//Recibir header
		int buffersize = sizeof(headerDeLosRipeados);
		void *buffer = malloc(buffersize);
		int bytesRecibidos = recv(servidor, buffer, buffersize, 0);
		if (bytesRecibidos <= 0) {
			free(buffer);
			logearError("Se desconectó el Kernel",true);
		}
		headerDeLosRipeados header;
		deserializarHeader(&header, buffer);
		free(buffer);

		int bytesDePayload = header.bytesDePayload;
		int codigoDeOperacion = header.codigoDeOperacion;

		//printf("Op:%d Bytes:%d\n",codigoDeOperacion, bytesDePayload); //Rico debug

		//Procesar operación del header
		procesarOperacion(codigoDeOperacion,bytesDePayload);
	}
	return NULL;
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
