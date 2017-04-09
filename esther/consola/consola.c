/*
 * consola.c
 *
 *  Created on: 1/4/2017
 *      Author: utnso
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "commons/log.h"
#include "commons/config.h"

#define MOSTRAR_LOGS_EN_PANTALLA true

#define RUTA_CONFIG "config.cfg"
#define RUTA_LOG "consola.log"

t_log* logger;
t_config* config;

enum CodigoDeOperacion {
	MENSAJE, CONSOLA, MEMORIA, FILESYSTEM, CPU
};

struct headerDeLosRipeados {
	unsigned short bytesDePayload;
	char codigoDeOperacion; // 0 (mensaje). Handshake: 1 (consola), 2 (memoria), 3 (filesystem), 4 (cpu), 5 (kernel)
};



int servidor; //kernel
char IP_KERNEL[16]; // 255.255.255.255 = 15 caracteres + 1 ('\0')
int PUERTO_KERNEL;

void imprimirOpcionesDeConsola();
void iniciarPrograma();
void desconectarPrograma();
void desconectarConsola();
void enviarMensaje();
void leerMensaje();
void limpiarPantalla();
void interaccionConsola();
void establecerConfiguracion();
void configurar(char* quienSoy);
void conectarAKernel();
void handshake(int servidor, char codOp);

int main(void) {

	configurar("consola");
	conectarAKernel();
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
	char mensaje[512] = "";
	scanf("%511[^\n]", mensaje);
	/*struct headerDeLosRipeados headerDeMiMensaje;
	headerDeMiMensaje.bytesDePayload = strlen(mensaje)+1;
	headerDeMiMensaje.codigoDeOperacion = MENSAJE;

	char *headerComprimido;
	headerComprimido = malloc(sizeof(headerDeMiMensaje));
	serializarHeader(&headerDeMiMensaje, headerComprimido);

	send(servidor, headerComprimido, strlen(headerComprimido), 0); // Mando el header primero
	free(headerComprimido);*/
	send(servidor, mensaje, strlen(mensaje)+1, 0); // Mando el mensaje después
}

void leerMensaje() {
	int bytesRecibidos;
	char mensaje[512];
	bytesRecibidos = recv(servidor,mensaje,sizeof(mensaje), 0);
	mensaje[bytesRecibidos]='\0';
	if(bytesRecibidos <= 0){
		close(servidor);
		log_error(logger,"Servidor desconectado luego de intentar leer mensaje");
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

			continue;
		}

		// limpiar buffer de entrada
		int c;
		while ( (c = getchar()) != '\n' && c != EOF );

		char opcion = input[0];

		switch(opcion) {
			case '1' : {
				iniciarPrograma(); // TODO
				log_info(logger,"Comando de inicio de programa ejecutado");
				break;
			}
			case '2' : {
				desconectarPrograma(); // TODO
				log_info(logger,"Comando de desconexión de programa ejecutado");
				break;
			}
			case '3' : {
				log_info(logger,"Comando de apagado de consola ejecutado\n");
				log_destroy(logger);
				desconectarConsola();
				break;
			}
			case '4' : {
				enviarMensaje();
				log_info(logger,"Comando de envío de mensaje ejecutado");
				leerMensaje();
				printf("\nMensaje completado, coloque otra opción. Opcion 6 para más información\n");
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

void establecerConfiguracion() {
	if(config_has_property(config, "PUERTO_KERNEL")){
		PUERTO_KERNEL = config_get_int_value(config, "PUERTO_KERNEL");;
		printf("Puerto Kernel: %d \n",PUERTO_KERNEL);
	}else{
		log_error(logger, "Error al leer el puerto del Kernel");
	}
	if(config_has_property(config, "IP_KERNEL")){
		strcpy(IP_KERNEL,config_get_string_value(config, "IP_KERNEL"));
		printf("IP Kernel: %s \n",IP_KERNEL);
	}else{
		log_error(logger, "Error al leer la IP del Kernel");
	}
}

int existeArchivo(const char *ruta)
{
    FILE *archivo;
    if ((archivo = fopen(ruta, "r")))
    {
        fclose(archivo);
        return true;
    }
    return false;
}

void conectarAKernel() {
	struct sockaddr_in direccionServidor;
	direccionServidor.sin_family = AF_INET;
	direccionServidor.sin_addr.s_addr = inet_addr( (char*) IP_KERNEL);
	direccionServidor.sin_port = htons(PUERTO_KERNEL);
	servidor = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(servidor, (struct sockaddr *) &direccionServidor,sizeof(direccionServidor)) < 0) {
		close(servidor);
		log_error(logger,"No se pudo conectar al Kernel");
		exit(0);
	}
	log_info(logger,"Conectado al Kernel");
}

void serializarHeader(struct headerDeLosRipeados *header, char *buffer) {
	/*//buffer = malloc(sizeof(struct headerDeLosRipeados)+1);
	 sprintf(*buffer,"%u %i", header.codigoDeOperacion, header.bytesDePayload);
	 printf("IMPRESION DE HANDY SERIALIZADO: %s\n", *buffer);
	 */
	short *cache = (short*) buffer;
	*cache = header->bytesDePayload;
	cache++;
	*cache = header->codigoDeOperacion;
}

void deserializarHeader(struct headerDeLosRipeados *header, char *buffer) {
	short *cache = (short*) buffer;
	header->bytesDePayload = *cache;
	cache++;
	header->codigoDeOperacion = *cache;
}

void handshake(int socket, char operacion) {
	printf("Conectando a servidor 0 porciento\n");
	/*	printf("Conectando a servidor 0 porciento\n");
	 printf("Conectando a servidor 23 porciento\n");
	 sleep(1);
	 printf("Conectando a servidor 55 porciento\n");
	 printf("Conectando a servidor 84 porciento\n");
	 sleep(2);*/
	struct headerDeLosRipeados handy;
	handy.bytesDePayload = 0;
	handy.codigoDeOperacion = operacion;

	int buffersize = sizeof(struct headerDeLosRipeados);
	// int buffersize = sizeof(struct headerDeLosRipeados) + 1;
	char *buffer = malloc(buffersize);
	//printf("%i\n", buffersize);
	//buffer = malloc(buffersize);

	//serializarHeader(handy,&buffer);
	//buffer[buffersize] = '\0';
	serializarHeader(&handy, buffer);
	//printf("%s\n", buffer);
	send(socket, (void*) buffer, buffersize, 0);

	// Recibe lo que responde el servidor
	char respuesta[1024];
	int bytesRecibidos = recv(socket, (void *) &respuesta, sizeof(respuesta), 0);

	if (bytesRecibidos > 0) {
		printf("Conectado a servidor 100 porciento\n");
		printf("Mensaje del servidor: \"%s\"\n", respuesta);
	}
	else {
		printf("Ripeaste\n");
		exit(0);
	}
	free(buffer);
}

void configurar(char* quienSoy) {

	//Esto es por una cosa rara del Eclipse que ejecuta la aplicación
	//como si estuviese en la carpeta esther/consola/
	//En cambio, en la terminal se ejecuta desde esther/consola/Debug
	//pero en ese caso no existiria el archivo config ni el log
	//y es por eso que tenemos que leerlo desde el directorio anterior

	if (existeArchivo(RUTA_CONFIG)) {
		config = config_create(RUTA_CONFIG);
		logger = log_create(RUTA_LOG, quienSoy, MOSTRAR_LOGS_EN_PANTALLA, LOG_LEVEL_INFO);
	}else{
		config = config_create(string_from_format("../%s",RUTA_CONFIG));
		logger = log_create(string_from_format("../%s",RUTA_LOG), quienSoy,MOSTRAR_LOGS_EN_PANTALLA, LOG_LEVEL_INFO);
	}

	//Si la cantidad de valores establecidos en la configuración
	//es mayor a 0, entonces configurar la ip y el puerto,
	//sino, estaría mal hecho el config.cfg

	if(config_keys_amount(config) > 0) {
		establecerConfiguracion();
	} else {
		log_error(logger, "Error al leer archivo de configuración");
	}
	config_destroy(config);
}
