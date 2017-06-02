#include "op_cpu.h"
#include "qepd/qepd.h"
#include "string.h"

int cpu_processar_operacion(int socket) {
	headerDeLosRipeados header;
	int ret = recibir_header(socket ,&header);

	if (ret <= 0) {
		return -1;
	}

	char operacion = header.codigoDeOperacion;

	switch (operacion) {
	case OBTENER_VALOR_VARIABLE:
		return cpu_obtener_variable(socket);

	case ASIGNAR_VALOR_VARIABLE:
		return 0;

	case INSTRUCCION:
		return 0;
	case SOLICITAR_BYTES:
		logear_info("CPU %d solicitando bytes...", socket);

		usleep(RETARDO * 1000);

		posicionDeMemoriaAPedir posicion_relativa;
		recv(socket, &posicion_relativa, header.bytesDePayload, 0);
		int frame = traducir_a_frame(posicion_relativa.numero_pagina, posicion_relativa.processID);
		if (frame >= 0) {
			enviar_header(socket, SOLICITAR_BYTES, posicion_relativa.size);
			int posicion_absoluta = frame * MARCO_SIZE + posicion_relativa.offset;
			send(socket, memoria+posicion_absoluta, posicion_relativa.size, 0);
			logear_info("Se le envió correctamente el contenido");
		} else {
			logear_error("CPU intentó acceder a posición de memoria indebida", false);
			enviar_header(socket, EXCEPCION, 4);
		}
		return 0;
	default:
		return -2;
	}
}

int cpu_obtener_variable(int socket) {

	posicionDeMemoriaVariable posicion; //Estructura del segundo send del CPU

	int ret = recv(socket, &posicion, sizeof posicion, 0);

	if (ret <= 0) {
		return -1;
	}

	if (buscarEnMemoriaYDevolver(posicion, socket) == -1) {

		printf("No se encontro en memoria"); //Habria que avisar al cpu
	}
	return -1;

}

void enviarDesdeCache(int indice, int socket, posicionDeMemoriaVariable posicion) {

	int posicionInicioACopiar = tablaAdministrativa_cache[indice].contenidoDeLaPag + posicion.start;
	int *buffer = malloc(sizeof (int));

	//memcpy(ir_a_frame_cache(indice) + posicion.start, buffer ,(posicion.offset + 1) - posicion.start);	En este memcpy uso la funcion ir a frame cache que me evita tener el offset (contenidoDePag) en la estructura

	memcpy(buffer, memoria_cache + posicionInicioACopiar , posicion.offset); // En este memcpy uso el contenidoDePag, no le veo la necesidad

	send(socket, buffer, 4, 0);

}

int traducir_a_frame(int pagina, int pid) {
	//la forma más básica de traducir a frame
	//en realidad habría que usar la función de hashing
	int i;
	for (i = 0; i < MARCOS; i++) {
		if (tablaAdministrativa[i].pid == pid
			&& tablaAdministrativa[i].pag == pagina) {
			return i;
		}
	}
	return -1;
}

