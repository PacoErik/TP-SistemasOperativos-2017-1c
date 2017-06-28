#include "op_cpu.h"
#include "qepd/qepd.h"
#include "string.h"

int cpu_procesar_operacion(int socket) {
	headerDeLosRipeados header;
	int numero_excepcion;
	int frame;
	int ret = recibir_header(socket ,&header);

	if (ret <= 0) {
		return -1;
	}

	char operacion = header.codigoDeOperacion;
	posicion_memoria_pedida posicion;

	switch (operacion) {

	case SOLICITAR_BYTES:
		recv(socket, &posicion, header.bytesDePayload, 0);

		char *buffer = (char*)memoria_solicitar_bytes(posicion.processID, posicion.numero_pagina, posicion.offset, posicion.size);

		if (buffer != NULL) {
			enviar_header(socket, SOLICITAR_BYTES, posicion.size);
			send(socket, buffer, posicion.size, 0);
			logear_info("[PID:%d] Se le envió correctamente el contenido a la CPU %d", posicion.processID,socket);
		} else {
			enviar_excepcion(socket, FALLO_DE_SEGMENTO);
			logear_error("[PID:%d] CPU %d intentó acceder a posición de memoria indebida", false, posicion.processID, socket);
		}

		return 0;

		break;
 	case ALMACENAR_BYTES:
		recv(socket, &posicion, header.bytesDePayload, 0);
		logear_info("[PID:%d] CPU %d almacenando bytes...", posicion.processID,socket);

		char *buffer_a_almacenar = malloc(posicion.size);
		recv(socket, buffer, posicion.size, 0);

		int respuesta = memoria_almacenar_bytes(posicion.processID, posicion.numero_pagina, posicion.offset, posicion.size, buffer_a_almacenar);

		if (respuesta < 0) {
			enviar_excepcion(socket, respuesta);
			logear_error("[PID:%d] CPU intentó acceder a posición de memoria indebida", posicion.processID,false);
		} else {
			enviar_header(socket, PETICION_CORRECTA, 0);
			logear_info("[PID:%d] CPU asignó correctamente el contenido", posicion.processID);
		}

		free(buffer_a_almacenar);

		return 0;

	default:
		return -2;
	}
}



