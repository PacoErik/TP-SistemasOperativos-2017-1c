#include <qepd/qepd.h>
#include <pthread.h>
#include "commons/collections/list.h"
#include "commons/process.h"

t_log* logger;
t_config* config;

int servidor; //kernel
char IP_KERNEL[16]; // 255.255.255.255 = 15 caracteres + 1 ('\0')
int PUERTO_KERNEL;

typedef struct proceso {
	int PID;
	pthread_t hiloID;
} proceso;

typedef t_list listaProceso;

listaProceso *procesos;

void imprimirOpcionesDeConsola();
static void* iniciarPrograma(void*);
void desconectarPrograma();
void desconectarConsola();
void enviarMensaje();
void limpiarPantalla();
void interaccionConsola();
void establecerConfiguracion();

int main(void) {

	procesos = list_create();
	configurar("consola");
	conectar(&servidor, IP_KERNEL, PUERTO_KERNEL);
	handshake(servidor, CONSOLA);
	interaccionConsola();

	return 0;
}

void agregarProceso(int PID, pthread_t hiloID) {
	proceso *nuevoProceso = malloc(sizeof(proceso));
	nuevoProceso->PID = PID;
	nuevoProceso->hiloID = hiloID;
	list_add(procesos, nuevoProceso);
}

void eliminarProceso(int PID) {
	_Bool mismoPID(void* elemento) {
		return PID == ((proceso *) elemento)->PID;
	}
	list_remove_and_destroy_by_condition(procesos, mismoPID, free);
}

void imprimirOpcionesDeConsola() {
	printf("\n--------------------\n");
	printf("\n");
	printf("BIEVENIDO A LA CONSOLA\n");
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

void* iniciarPrograma(void* _) {
	unsigned int TID = process_get_thread_id();
	//unsigned int PID = process_getpid();
	printf("\nPrograma ejecutandose en Hilo %u\n", TID);
	conectar(&servidor, IP_KERNEL, PUERTO_KERNEL);
	handshake(servidor, CONSOLA);

	int size = strlen("Thread ID XX") + 1;
	char mensaje[size];
	sprintf(mensaje, "Thread ID %2d", TID);

	headerDeLosRipeados headerDeMiMensaje;
	headerDeMiMensaje.bytesDePayload = size;
	headerDeMiMensaje.codigoDeOperacion = MENSAJE;

	int headerSize = sizeof(headerDeMiMensaje);
	void *headerComprimido = malloc(headerSize);
	serializarHeader(&headerDeMiMensaje, headerComprimido);

	send(servidor, headerComprimido, headerSize, 0);
	free(headerComprimido);

	send(servidor, mensaje, size, 0);

	for(;;);
	return NULL;
}

void desconectarPrograma() {

//	pthread_cancel(hiloID);
}

void desconectarConsola() {
	close(servidor);
	exit(0);
}

void enviarMensaje() {
	char mensaje[512] = "";

	EscribirMensaje: printf("\nEscribir mensaje: ");
	scanf("%511[^\n]", mensaje);

	if (strlen(mensaje) == 0) {
		printf("Capo, hacé bien el mensaje"); // El mensaje no puede ser vacio
		// Limpiar buffer de entrada
		int c;
		while ((c = getchar()) != '\n' && c != EOF)
			;
		goto EscribirMensaje;
	}

	headerDeLosRipeados headerDeMiMensaje;
	headerDeMiMensaje.bytesDePayload = strlen(mensaje);
	headerDeMiMensaje.codigoDeOperacion = MENSAJE;

	int headerSize = sizeof(headerDeMiMensaje);
	void *headerComprimido = malloc(headerSize);
	serializarHeader(&headerDeMiMensaje, headerComprimido);

	send(servidor, headerComprimido, headerSize, 0); // Mando el header primero
	free(headerComprimido);

	send(servidor, mensaje, strlen(mensaje) + 1, 0); // Mando el mensaje después

	// El server retransmite el mensaje
	//leerMensaje();
	//Pero el kernel no se lo retransmite a la Consola (los otros procesos
	//esperan mensajes, este proceso espera que el usuario haga algo)
}

void leerMensaje() {

	int bytesRecibidos;
	char mensaje[512];
	bytesRecibidos = recv(servidor, &mensaje, sizeof(mensaje), 0);
	mensaje[bytesRecibidos] = '\0';
	if (bytesRecibidos <= 0 || !strncmp(mensaje, "Error, desconectado", 18)) {
		close(servidor);
		logearError("Servidor desconectado luego de intentar leer mensaje",
				true);
	}
	logearInfo("Mensaje recibido: %s\n", mensaje);
}

void limpiarPantalla() {
	printf("\033[H\033[J");
}

void interaccionConsola() {
	imprimirOpcionesDeConsola();
	char input[3];
	while (1) {
		scanf("%2s", input);

		// limpiar buffer de entrada
		int c;
		while ((c = getchar()) != '\n' && c != EOF);

		// Si lo que ingresa el usuario tiene mas de un caracter o no es numero
		if ((strlen(input) != 1) || '1' > input[0] || input[0] > '6') {
			printf("\nColoque una opcion correcta (1, 2, 3, 4, 5 o 6)\n");

			continue;
		}

		char opcion = input[0];

		switch (opcion) {
		case '1': {
			pthread_t hiloPrograma;
			pthread_create(&hiloPrograma, NULL, iniciarPrograma, NULL);
			logearInfo("Comando de inicio de programa ejecutado");
			break;
		}
		case '2': {
			desconectarPrograma(); // TODO
			logearInfo("Comando de desconexión de programa ejecutado");
			break;
		}
		case '3': {
			logearInfo("Comando de apagado de consola ejecutado\n");
			log_destroy(logger);
			desconectarConsola();
			break;
		}
		case '4': {
			enviarMensaje();
			logearInfo("Comando de envío de mensaje ejecutado\n");
			leerMensaje();
			logearInfo(
					"Mensaje completado, coloque otra opción. Opcion 6 para más información\n");
			break;
		}
		case '5': {
			limpiarPantalla();
			break;
		}
		case '6': {
			imprimirOpcionesDeConsola();
			break;
		}
		}
	}
}

void establecerConfiguracion() {
	if (config_has_property(config, "PUERTO_KERNEL")) {
		PUERTO_KERNEL = config_get_int_value(config, "PUERTO_KERNEL");
		;
		logearInfo("Puerto Kernel: %d \n", PUERTO_KERNEL);
	} else {
		logearError("Error al leer el puerto del Kernel", true);
	}
	if (config_has_property(config, "IP_KERNEL")) {
		strcpy(IP_KERNEL, config_get_string_value(config, "IP_KERNEL"));
		logearInfo("IP Kernel: %s \n", IP_KERNEL);
	} else {
		logearError("Error al leer la IP del Kernel", true);
	}
}
