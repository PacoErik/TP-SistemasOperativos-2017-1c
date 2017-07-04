#include "op_kernel.h"

int kernel_procesar_operacion() {
	op_kernel operacion;

	int ret = recv(socket_kernel, &operacion, sizeof(operacion), 0);

	if (ret <= 0) {
		return -1;
	}

	switch (operacion) {

	case MEM_INICIALIZAR_PROGRAMA:
		return kernel_inicializar_programa();

	case MEM_ASIGNAR_PAGINAS:
		return kernel_asignar_paginas();

	case MEM_FINALIZAR_PROGRAMA:
		return kernel_finalizar_programa();

	case MEM_ALMACENAR_BYTES:
		return kernel_almacenar_bytes();

	case MEM_LIBERAR_PAGINA:
		return kernel_liberar_pagina();

	case MEM_SOLICITAR_BYTES:
		return kernel_solicitar_bytes();

	default:
		return -2;

	}

}

int kernel_inicializar_programa() {
	int respuesta;
	PedidoInicializar paquete;

	respuesta = recv(socket_kernel, &paquete, sizeof(paquete), 0);
	if (respuesta <= 0) {
		return -1;
	}

	logear_info("[PID:%d] Verificando espacio para nuevo programa...", paquete.PID);
	respuesta = memoria_inicializar_programa(paquete.PID, paquete.paginas_codigo + paquete.paginas_stack);
	if (respuesta < 0) {
		logear_info("[PID:%d] Memoria insuficiente.", paquete.PID);
	} else {
		logear_info("[PID:%d] Listo.", paquete.PID);
	}

	send(socket_kernel, &respuesta, sizeof(respuesta), 0);

	return 1;
}

int kernel_asignar_paginas() {

	int respuesta;
	PedidoAsignacion paquete;

	respuesta = recv(socket_kernel, &paquete, sizeof paquete, 0);
	if (respuesta <= 0) {
		return -1;
	}

	logear_info("[PID:%d] Asignando páginas al programa...", paquete.PID);

	respuesta = memoria_asignar_paginas(paquete.PID, paquete.paginas);

	if (respuesta < 0) {
		logear_info("[PID:%d] No se pudieron asignar más páginas.", paquete.PID);
	} else {
		logear_info("[PID:%d] Éxito al asignar páginas.", paquete.PID);
	}

	send(socket_kernel, &respuesta, sizeof(respuesta), 0);

	return 1;
}

int kernel_finalizar_programa() {
	PedidoFinalizacion paquete;

	int respuesta = recv(socket_kernel, &paquete, sizeof(paquete), 0);

	if (respuesta <= 0) {
		return -1;
	}

	logear_info("[PID:%d] Borrando páginas del proceso...", paquete.PID);

	respuesta = memoria_finalizar_programa(paquete.PID);
	//Por ahora se asume que nunca habría problemas al finalizar un programa.
	logear_info("[PID:%d] Borrado correctamente.", paquete.PID);
	send(socket_kernel, &respuesta, sizeof(respuesta), 0);

	return 1;
}

int kernel_solicitar_bytes() {
	PedidoLectura paquete;

	int respuesta = recv(socket_kernel, &paquete, sizeof(paquete), 0);

	if (respuesta <= 0) {
		return -1;
	}

	logear_info("[PID:%d] Kernel solicitando %d bytes...", paquete.PID, paquete.size);

	char *datos = (char*)memoria_solicitar_bytes(paquete.PID, paquete.numero_pagina, paquete.offset, paquete.size);

	if (datos == NULL) {
		logear_error("[PID:%d] Kernel intentó acceder a posición de memoria indebida", false, paquete.PID);
		respuesta = FALLO_DE_SEGMENTO;
	} else {
		logear_info("[PID:%d] Se le envió correctamente el contenido al Kernel", paquete.PID);
		respuesta = true;
	}

	send(socket_kernel, &respuesta, sizeof(respuesta), 0);

	if (respuesta == true)
		send(socket_kernel, datos, paquete.size, 0);

	return 1;
}

int kernel_almacenar_bytes() {
	PedidoEscritura paquete;

	int respuesta = recv(socket_kernel, &paquete, sizeof(paquete), 0);
	if (respuesta <= 0) {
		return -1;
	}

	paquete.datos = malloc(paquete.size);
	recv(socket_kernel, paquete.datos, paquete.size, 0);

	logear_info("[PID:%d] Kernel almacenando %d bytes...", paquete.PID, paquete.size);

	respuesta = memoria_almacenar_bytes(paquete.PID, paquete.numero_pagina, paquete.offset, paquete.size, paquete.datos);
	if (respuesta < 0) {
		logear_error("[PID:%d] Kernel intentó acceder a posición de memoria indebida", false, paquete.PID);
	} else {
		logear_info("[PID:%d] Kernel asignó correctamente el contenido", paquete.PID);
	}

	send(socket_kernel, &respuesta, sizeof(respuesta), 0);


	free(paquete.datos);
	return 1;
}


int kernel_liberar_pagina() {
	PedidoLiberacion paquete;

	int respuesta = recv(socket_kernel, &paquete, sizeof(paquete), 0);
	if (respuesta <= 0) {
		return -1;
	}

	logear_info("[PID:%d] Kernel liberando la página %d", paquete.PID, paquete.numero_pagina);

	respuesta = memoria_liberar_pagina(paquete.PID, paquete.numero_pagina);

	if (respuesta < 0) {
		logear_error("Error al liberar página %d de (PID:%d)", false, paquete.numero_pagina, paquete.PID);
	} else {
		logear_info("Página %d de (PID:%d) liberada correctamente!", paquete.numero_pagina, paquete.PID);
	}

	send(socket_kernel, &respuesta, sizeof(respuesta), 0);

	return 1;
}

