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
#define RUTA_LOG "cpu.log"

t_log* logger;
t_config* config;

enum CodigoDeOperacion {
	MENSAJE, CONSOLA, MEMORIA, FILESYSTEM, CPU
};

struct headerDeLosRipeados {
	unsigned short bytesDePayload;
	char codigoDeOperacion; // 0 (mensaje). Handshake: 1 (consola), 2 (memoria), 3 (filesystem), 4 (cpu), 5 (kernel)
}__attribute__((packed, aligned(1)));

int servidor; //kernel
char IP_KERNEL[16]; // 255.255.255.255 = 15 caracteres + 1 ('\0')
int PUERTO_KERNEL;
char IP_MEMORIA[16];
int PUERTO_MEMORIA;

void desconectarConsola();
void leerMensaje();
void establecerConfiguracion();
void configurar(char*);
void conectarAKernel();
void handshake(int,char);
void serializarHeader(struct headerDeLosRipeados *, char *);
void deserializarHeader(struct headerDeLosRipeados *, char *);

int main(void) {

	configurar("cpu");
	conectarAKernel();
	handshake(servidor, CPU);
	while (1){
		leerMensaje();
	}

	return 0;
}


void desconectarConsola() {
	close(servidor);
	exit(EXIT_SUCCESS);
}

void leerMensaje() {
	int bytesRecibidos;
	char mensaje[512];
	bytesRecibidos = recv(servidor,&mensaje,sizeof(mensaje), 0);
	mensaje[bytesRecibidos]='\0';
	if(bytesRecibidos <= 0){
		close(servidor);
		log_error(logger,"Servidor desconectado luego de intentar leer mensaje");
		exit(0);
	}
	printf("\nMensaje recibido: %s\n",mensaje);
}

void establecerConfiguracion() {
	if(config_has_property(config, "PUERTO_KERNEL")){
		PUERTO_KERNEL = config_get_int_value(config, "PUERTO_KERNEL");
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
	if(config_has_property(config, "PUERTO_MEMORIA")){
		PUERTO_MEMORIA = config_get_int_value(config, "PUERTO_MEMORIA");
		printf("Puerto Memoria: %d \n",PUERTO_MEMORIA);
	}else{
		log_error(logger, "Error al leer el puerto de la Memoria");
	}
	if(config_has_property(config, "IP_MEMORIA")){
		strcpy(IP_MEMORIA,config_get_string_value(config, "IP_MEMORIA"));
		printf("IP Memoria: %s \n",IP_MEMORIA);
	}else{
		log_error(logger, "Error al leer la IP de la Memoria");
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

void handshake(int socket, char operacion) {
	printf("Conectando a servidor 0 porciento\n");
	struct headerDeLosRipeados handy;
	handy.bytesDePayload = 0;
	handy.codigoDeOperacion = operacion;

	int buffersize = sizeof(struct headerDeLosRipeados);
	char *buffer = malloc(buffersize);
	serializarHeader(&handy, buffer);
	send(socket, (void*) buffer, buffersize, 0);

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

void serializarHeader(struct headerDeLosRipeados *header, char *buffer) {
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