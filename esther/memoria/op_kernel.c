#include "op_kernel.h"

int kernel_processar_operacion(int socket) {
	op_kernel operacion;

	int ret = recv(socket, &operacion, sizeof operacion, 0);

	if (ret <= 0) {
		return -1;
	}

	switch (operacion) {
	case MEM_INICIALIZAR_PROGRAMA:
		return kernel_inicializar_programa(socket);

	case MEM_ASIGNAR_PAGINAS:
		return kernel_asignar_paginas(socket);

	case MEM_FINALIZAR_PROGRAMA:
		return kernel_finalizar_programa(socket);

	case MEM_MENSAJE:
		return kernel_mensaje(socket);

	default:
		return 0;
	}
}

int kernel_mensaje(int socket) {
	int tamanio;
	int ret = recv(socket, &tamanio, sizeof tamanio, 0);

	if (ret <= 0) {
		return -1;
	}

	char *mensaje = malloc(tamanio);
	ret = recv(socket, mensaje, tamanio, 0);

	if (ret <= 0) {
		return -1;
	}

	logearInfo("Mensaje recibido: %s", mensaje);

	free(mensaje);
	return 1;
}

int kernel_inicializar_programa(int socket) {
	PedidoInicializar paquete;

	int ret = recv(socket, &paquete, sizeof paquete, 0);

	if (ret <= 0) {
		return -1;
	}

	void *datos_programa = malloc(paquete.bytes_datos);

	ret = recv(socket, datos_programa, paquete.bytes_datos, 0);

	if (ret <= 0) {
		return 0;
	}

	ret = asignar_frames_contiguos(paquete.PID, paquete.paginas,
			paquete.bytes_datos, datos_programa);

	printf("[PID %d] Codigo del Proceso:\n%.*s \n", paquete.PID, paquete.bytes_datos, (char *) datos_programa);

	free(datos_programa);

	respuesta_op_kernel respuesta;

	if (ret == 0) {
		respuesta = ERROR;
		send(socket, &respuesta, sizeof respuesta, 0);
	}

	respuesta = OK;
	send(socket, &respuesta, sizeof respuesta, 0);

	logearInfo("[PID %d] Listo.", paquete.PID);

	return 1;
}

int kernel_asignar_paginas(int socket) {
	PedidoAsignacion paquete;

	int ret = recv(socket, &paquete, sizeof paquete, 0);

	if (ret <= 0) {
		return -1;
	}

	printf("TODO: Asignar %d paginas al PID %d\n", paquete.paginas, paquete.PID);

	/* TODO: Enviar confirmacion */

	return 1;
}

int kernel_finalizar_programa(int socket) {
	PedidoFinalizacion paquete;

	int ret = recv(socket, &paquete, sizeof paquete, 0);

	if (ret <= 0) {
		return -1;
	}

	liberar_frames(paquete.PID);

	logearInfo("[PID %d] Eliminado.", paquete.PID);

	return 1;
}
