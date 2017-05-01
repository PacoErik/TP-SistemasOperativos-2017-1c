//-----HEADERS-----//
#include <qepd/qepd.h>
#include <pthread.h>
#include "commons/collections/list.h"
#include "commons/process.h"
#include <sys/types.h>

//-----DEFINES-----//
#define DURACION(INICIO) ((double)(clock() - INICIO) / CLOCKS_PER_SEC)

//-----ESTRUCTURAS-----//
typedef struct proceso {
	int PID;
	pthread_t hiloID;
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
void 			agregarProceso(int, pthread_t);
void 			configurarPrograma();
void 			confirmarComando();
void 			desconectarConsola();
void 			desconectarPrograma();
void 			eliminarProceso(int);
void 			enviarHeader(int, char, int);
void 			enviarMensaje();
void 			establecerConfiguracion();
pthread_t		hiloIDPrograma(int);
void 			imprimirOpcionesDeConsola();
static void* 	iniciarPrograma(void*);
void 			interaccionConsola();
void 			leerMensaje();
void 			limpiarBufferEntrada();
void 			limpiarPantalla();
char*			remove_newline(char*);
int				soloNumeros(char*);

//-----PROCEDIMIENTO PRINCIPAL-----//
int main(void) {

	procesos = list_create();
	configurar("consola");
	conectar(&servidor, IP_KERNEL, PUERTO_KERNEL);
	handshake(servidor, CONSOLA);
	interaccionConsola();

	return 0;
}

//-----DEFINICIÓN DE FUNCIONES-----
void agregarProceso(int PID, pthread_t hiloID) {
	proceso *nuevoProceso = malloc(sizeof(proceso));
	nuevoProceso->PID = PID;
	nuevoProceso->hiloID = hiloID;
	list_add(procesos, nuevoProceso);
}
void configurarPrograma() {
	char* ruta = calloc(64,sizeof(char));
	//calloc es similar a malloc pero inicializa cada valor a 0
	printf("Ingresar ruta: ");
	fgets(ruta, 64, stdin);
	remove_newline(ruta);

	pthread_attr_t attr;
	pthread_t hiloPrograma;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	pthread_create(&hiloPrograma, &attr, &iniciarPrograma, ruta);

	pthread_attr_destroy(&attr);

	int PID;
	recv(servidor, &PID, sizeof PID, 0);

	if (PID == -1) {
		printf("No se pudo añadir proceso\n");
		return;
	}
	printf("Hilo ID: %lu\n", hiloPrograma);
	printf("PID: %d\n", PID);
	agregarProceso(PID, hiloPrograma);
}
void confirmarComando() {
	leerMensaje();
	logearInfo("Comando completado, coloque otra opción. Opcion 6 para más información\n");
}
void desconectarConsola() {
	void _finalizar(void *element) {
		proceso *proc = (proceso*) element;
		int PID = proc->PID;
		desconectarPrograma(PID);
	}
	list_destroy_and_destroy_elements(procesos, _finalizar);

	close(servidor);
	exit(0);
}
void desconectarPrograma(int PID) {
	printf("Finalizando PID %d...\n", PID);
	pthread_t TID = hiloIDPrograma(PID);
	if (TID == 0) {
		logearError("No existe PID %d\n", false, PID);
		return;
	}
	enviarHeader(servidor, FINALIZAR_PROGRAMA, sizeof PID);
	send(servidor, &PID, sizeof PID, 0);

	pthread_cancel(TID);
	pthread_join(TID, NULL);

	eliminarProceso(PID);
	printf("El programa fue finalizado\n");

	//	TODO
	//	[X] Matar al hilo correspondiente al programa
	//	[ ] Calcular su duración en el sistema
	//	[X] Avisarle al Kernel que mate a dicho programa, liberando las estructuras
	//	    que ocupó en memoria y borrando su PCB
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
void enviarMensaje() {
	char mensaje[512] = "";

	do {
		printf("\nEscribir mensaje: ");
		fgets(mensaje, sizeof mensaje, stdin);
		remove_newline(mensaje);
		if (strlen(mensaje) == 0) {
			printf("Capo, hacé bien el mensaje"); // El mensaje no puede ser vacio
		}
	} while (strlen(mensaje) == 0);

	int bytes = strlen(mensaje);

	enviarHeader(servidor,MENSAJE,bytes);
	send(servidor, mensaje, bytes, 0);
}
void establecerConfiguracion() {
	if (config_has_property(config, "PUERTO_KERNEL")) {
		PUERTO_KERNEL = config_get_int_value(config, "PUERTO_KERNEL");
		logearInfo("Puerto Kernel: %d \n", PUERTO_KERNEL);
	} else {
		logearError("Error al leer el puerto del Kernel\n", true);
	}
	if (config_has_property(config, "IP_KERNEL")) {
		strcpy(IP_KERNEL, config_get_string_value(config, "IP_KERNEL"));
		logearInfo("IP Kernel: %s \n", IP_KERNEL);
	} else {
		logearError("Error al leer la IP del Kernel\n", true);
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
void imprimirOpcionesDeConsola() {
	printf("\n--------------------\n");
	printf("\n");
	printf("BIENVENIDO A LA CONSOLA\n");
	printf("SUS OPCIONES:\n");
	printf("\n");
	printf("1. Iniciar programa AnSISOP\n");
	printf("2. Finalizar programa AnSISOP\n");
	printf("3. Desconectar consola\n");
	printf("4. Enviar mensaje\n");
	printf("5. Limpiar mensajes\n");
	printf("6. Mostrar opciones nuevamente\n");
	printf("\n");
	printf("--------------------\n");
}
void* iniciarPrograma(void* arg) {
	//Inicio del programa
	clock_t inicio = clock();
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	pthread_t id_hilo = pthread_self();

	//Chequeo de que el archivo del programa ingresado exista
	char* ruta = arg;
	logearInfo("Ruta ingresada:%s\n",ruta);
	if (!existeArchivo(ruta)) {
		logearError("No se encontró el archivo %s\n",false,ruta);
		return NULL;
	}

	//Lectura y envío del programa
	FILE* prog = fopen(ruta,"r");

	char* codigo = NULL;
	long int bytes;
	//size_t bytes;
	//ssize_t bytes_leidos = getdelim( &codigo, &bytes, '\0', prog);

	fseek(prog, 0, SEEK_END);
	bytes = ftell(prog);
	fseek(prog, 0, SEEK_SET);
	codigo = malloc(bytes);
	fread(codigo, 1, bytes, prog);
	fclose(prog);

	int PID;
	//printf("Codigo:%s\nBytes:%i\nBytes_L:%i\nBytes_posta:%i\n",codigo,bytes,bytes_leidos,strlen(codigo));
	if (bytes > 0) {
		enviarHeader(servidor, INICIAR_PROGRAMA, bytes);
		send(servidor, &id_hilo, sizeof id_hilo, 0);
		send(servidor, codigo, bytes, 0);
		confirmarComando();
	} else if (bytes == 0) {
		logearError("Archivo vacio: %s\n", false, ruta);
		return NULL;
	} else {
		logearError("No se pudo leer el archivo: %s\n", false, ruta);
		return NULL;
	}

	free(arg); //ya no necesitamos más la ruta
	free(codigo); //ya no necesitamos más el código

	//Testing: esto es solo para chequear que el programa se envió y el kernel respondió
	logearInfo("Programa leido, enviado y confirmación recibida en %f segundos\n", DURACION(inicio));
	//Fin-testing

	for(;;);
	return NULL;
}

void interaccionConsola() {
	enum OpcionConsola {
		EJECUTAR_PROGRAMA=1, DESCONECTAR_PROGRAMA, DESCONECTAR_CONSOLA,
		ENVIAR_MENSAJE, LIMPIAR_PANTALLA, IMPRIMIR_OPCIONES
	};
	imprimirOpcionesDeConsola();
	char input[3];
	while (1) {
		memset(input, 0, sizeof input);
		fgets(input, sizeof input, stdin);
		if (strlen(input) == 1) {
			continue;
		}
		remove_newline(input);

		int opcion = input[0] - '0';

		// Si lo que ingresa el usuario tiene mas de un caracter o no es numero
		if ((strlen(input) != 1) || EJECUTAR_PROGRAMA > opcion
				|| opcion > IMPRIMIR_OPCIONES) {
			printf("\nColoque una opcion correcta (1, 2, 3, 4, 5 o 6)\n");
			limpiarBufferEntrada();
			continue;
		}

		switch (opcion) {
			case EJECUTAR_PROGRAMA: {
				logearInfo("Comando de inicio de programa ejecutado\n");
				configurarPrograma();
				break;
			}
			case DESCONECTAR_PROGRAMA: {
				logearInfo("Comando de desconexión de programa ejecutado\n");
				char sPID[12]; // String que representa PID
				printf("Ingresar PID: ");
				fgets(sPID, sizeof sPID, stdin);
				if (strlen(sPID) == 1) {
					logearError("PID invalido\n", false);
				}
				if (sPID[strlen(sPID) - 1] != '\n') {
					logearError("PID no puede tener mas de 10 digitos\n", false);
					limpiarBufferEntrada();
					break;
				}
				remove_newline(sPID);
				if (!soloNumeros(sPID)) {
					logearError("PID debe ser un numero\n", false);
					break;
				}
				int PID = strtoul(sPID, NULL, 0);
				desconectarPrograma(PID);
				break;
			}
			case DESCONECTAR_CONSOLA: {
				logearInfo("Comando de apagado de consola ejecutado\n");
				log_destroy(logger);
				desconectarConsola();
				break;
			}
			case ENVIAR_MENSAJE: {
				logearInfo("Comando de envío de mensaje ejecutado\n");
				enviarMensaje();
				confirmarComando();
				break;
			}
			case LIMPIAR_PANTALLA: {
				limpiarPantalla();
				break;
			}
			case IMPRIMIR_OPCIONES: {
				imprimirOpcionesDeConsola();
				break;
			}
		}
	}
}
void leerMensaje() {
	int bytesRecibidos;
	char mensaje[512];
	bytesRecibidos = recv(servidor, &mensaje, sizeof(mensaje), 0);
	mensaje[bytesRecibidos] = '\0';
	if (bytesRecibidos <= 0 || !strncmp(mensaje, "Error, desconectado", 18)) {
		close(servidor);
		logearError("Servidor desconectado luego de intentar leer mensaje\n",
				true);
	}
	logearInfo("Mensaje recibido: %s\n", mensaje);
}
void limpiarBufferEntrada() {
	int c;
	while ((c = getchar()) != '\n' && c != EOF);
}
void limpiarPantalla() {
	printf("\033[H\033[J");
}
char* remove_newline(char* s) { // By Beej
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
