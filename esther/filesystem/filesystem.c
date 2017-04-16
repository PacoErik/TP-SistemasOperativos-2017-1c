/*
 * filesystem.c
 *
 *  Created on: 13/4/2017
 *      Author: utnso
 */

#include <qepd/qepd.h>

t_log* logger;
t_config* config;

int servidor; //kernel
char IP_KERNEL[16]; // 255.255.255.255 = 15 caracteres + 1 ('\0')
int PUERTO_KERNEL;
char IP_MEMORIA[16];
int PUERTO_MEMORIA;

void leerMensaje();
void establecerConfiguracion();

int main(void) {

	configurar("filesystem");
	conectarAKernel();
	handshake(servidor, FILESYSTEM);
	while (1){
		leerMensaje();
	}

	return 0;
}

void leerMensaje() {

	int bytesRecibidos;
	char mensaje[512];
	bytesRecibidos = recv(servidor, &mensaje, sizeof(mensaje), 0);
	mensaje[bytesRecibidos]='\0';
	if(bytesRecibidos <= 0){
		close(servidor);
		logearError("Servidor desconectado luego de intentar leer mensaje",true);
	}
	logearInfo("Mensaje recibido: %s\n",mensaje);
}

void establecerConfiguracion() {
	if(config_has_property(config, "PUERTO_KERNEL")) {
		PUERTO_KERNEL = config_get_int_value(config, "PUERTO_KERNEL");
		logearInfo("Puerto Kernel: %d \n",PUERTO_KERNEL);
	} else {
		logearError("Error al leer el puerto del Kernel",true);
	}
	if(config_has_property(config, "IP_KERNEL")) {
		strcpy(IP_KERNEL,config_get_string_value(config, "IP_KERNEL"));
		logearInfo("IP Kernel: %s \n",IP_KERNEL);
	} else {
		logearError("Error al leer la IP del Kernel",true);
	}
	if(config_has_property(config, "PUERTO_MEMORIA")) {
		PUERTO_MEMORIA = config_get_int_value(config, "PUERTO_MEMORIA");
		logearInfo("Puerto Memoria: %d \n",PUERTO_MEMORIA);
	} else {
		logearError("Error al leer el puerto de la Memoria",true);
	}
}
