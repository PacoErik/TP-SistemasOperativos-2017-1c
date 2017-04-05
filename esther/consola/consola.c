/*
 * consola.c
 *
 *  Created on: 1/4/2017
 *      Author: utnso
 */

#include "operadores_header_serializador_handshake.h"
#include "commons/log.h"
#include "commons/config.h"
#include "commons/string.h"

#define RUTA_CONFIG "./config.cfg"
#define RUTA_LOG "./consola.log"

int servidor; //kernel
char IP_KERNEL[16]; // 255.255.255.255 = 15 caracteres + 1 ('\0')
int PUERTO_KERNEL;
t_log* log;
t_config* config;

void imprimirOpcionesDeConsola();
void iniciarPrograma();
void desconectarPrograma();
void desconectarConsola();
void enviarMensaje();
void leerMensaje();
void limpiarPantalla();
void interaccionConsola();

void establecerConfiguracion() {
	if(config_has_property(config, "PUERTO_KERNEL")){
		int port = config_get_int_value(config, "PUERTO_KERNEL");
		PUERTO_KERNEL = port;
		printf("\nPuerto Kernel: %d \n",PUERTO_KERNEL);
	}else{
		log_error(log, "Error al leer el puerto del Kernel");
	}
	if(config_has_property(config, "IP_KERNEL")){
		strcpy(IP_KERNEL,config_get_string_value(config, "IP_KERNEL"));
		printf("IP Kernel: %s \n",IP_KERNEL);
	}else{
		log_error(log, "Error al leer la IP del Kernel");
	}
}

void configurar(char *path_File, char *path_log) {
	log = log_create(RUTA_LOG, "consola",true, LOG_LEVEL_INFO);
	if(config_keys_amount(config = config_create(path_File)) > 0) {
		establecerConfiguracion();
	} else {
		log_error(log, "Error al leer archivo de configuración");
	}
	config_destroy(config);

	struct sockaddr_in direccionServidor;
	direccionServidor.sin_family = AF_INET;
	direccionServidor.sin_addr.s_addr = inet_addr("127.0.0.1");
	direccionServidor.sin_port = htons(8081);
	servidor = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(servidor, (struct sockaddr *) &direccionServidor,sizeof(direccionServidor)) < 0) {
		close(servidor);
		log_info(log,"No se pudo conectar al Kernel");
		exit(0);
	}
	log_info(log,"Conectado al Kernel");
}



int main(void) {
	configurar(RUTA_CONFIG, RUTA_LOG);
	handshake(servidor, CONSOLA);
	interaccionConsola();

	return 0;
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

void iniciarPrograma() {
	printf("TODO\n");
} // TODO

void desconectarPrograma() {
	printf("TODO\n");
} // TODO

void desconectarConsola() {
	close(servidor);
	exit(EXIT_SUCCESS);
}

void enviarMensaje() {
	printf("\nEscribir mensaje: ");
	char mensaje[512];
	scanf("%s", &mensaje);
	struct headerDeLosRipeados headerDeMiMensaje;
	headerDeMiMensaje.bytesDePayload = sizeof(mensaje);
	headerDeMiMensaje.codigoDeOperacion = MENSAJE;

	char *headerComprimido;
	headerComprimido = malloc(sizeof(headerDeMiMensaje));
	serializarHeader(&headerDeMiMensaje, headerComprimido);

	send(servidor, headerComprimido, strlen(headerComprimido), 0); // Mando el header primero
	send(servidor, mensaje, strlen(mensaje), 0); // Mando el mensaje después

	printf("\nMensaje enviado, coloque otra opción. Opcion 6 para más información\n");
}

void leerMensaje() {
	int bytesRecibidos;
	char mensaje[512];
	bytesRecibidos = recv(servidor,mensaje,sizeof(mensaje), 0);
	mensaje[bytesRecibidos]='\0';
	if(bytesRecibidos <= 0){
		close(servidor);
		log_info(log,"Servidor desconectado luego de intentar leer mensaje");
		exit(0);
	}
	printf("\nMensaje recibido: %s\n",mensaje);
}

void limpiarPantalla() {
	printf("\033[H\033[J");
}

void interaccionConsola() {
	imprimirOpcionesDeConsola();
	char input[3];
	while(1) {
		scanf("%2s", &input);

		// Si lo que ingresa el usuario tiene mas de un caracter o no es numero
		if ( (strlen(input) != 1)
				|| !isdigit(input[0]) ) {
			printf("\nColoque una opcion correcta (1, 2, 3, 4, 5 o 6)\n");

			// limpiar buffer de entrada
			int c;
			while ( (c = getchar()) != '\n' && c != EOF );

			continue;
		}

		char opcion = input[0];

		switch(opcion) {
			case '1' : {
				iniciarPrograma(); // TODO
				log_info(log,"Comando de inicio de programa ejecutado");
				break;
			}
			case '2' : {
				desconectarPrograma(); // TODO
				log_info(log,"Comando de desconexión de programa ejecutado");
				break;
			}
			case '3' : {
				log_info(log,"Comando de apagado de consola ejecutado");
				desconectarConsola();
				break;
			}
			case '4' : {
				enviarMensaje();
				leerMensaje();
				log_info(log,"Comando de envío de mensaje ejecutado");
				break;
			}
			case '5' : {
				limpiarPantalla();
				break;
			}
			case '6' : {
				imprimirOpcionesDeConsola();
				break;
			}
			default : {
				printf("\nColoque una opcion correcta (1, 2, 3, 4, 5 o 6)\n");
			}
		}
	}
}
