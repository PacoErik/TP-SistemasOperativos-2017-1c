#include "op_memoria.h"

int socket_memoria;

int tamanio_pagina;

int STACK_SIZE;

char IP_MEMORIA[16];

int PUERTO_MEMORIA;

void mem_conectar(void) {
	struct sockaddr_in mem_info;

	mem_info.sin_family = AF_INET;
	mem_info.sin_addr.s_addr = inet_addr( (char*) IP_MEMORIA);
	mem_info.sin_port = htons(PUERTO_MEMORIA);

	socket_memoria = socket(AF_INET, SOCK_STREAM, 0);

	int ret = connect(socket_memoria, (struct sockaddr *) &mem_info, sizeof(mem_info));

	if (ret != 0) {
		terminar_kernel();
	}

	handshake(socket_memoria, KERNEL);

	/* Recibe tamanio de pagina enviada por la memoria */
	if (recv(socket_memoria, &tamanio_pagina, sizeof(tamanio_pagina), 0) <= 0) {
		terminar_kernel();
	}
}

void mem_mensaje(char *mensaje) {
	op_mem operacion = MEM_MENSAJE;
	int tamanio = strlen(mensaje) + 1;
	send(socket_memoria, &operacion, sizeof operacion, 0);
	send(socket_memoria, &tamanio, sizeof tamanio, 0);
	send(socket_memoria, mensaje, tamanio, 0);
}

int mem_inicializar_programa(int PID, size_t size, void *datos) {
	int paginas_codigo = DIVIDE_ROUNDUP(size, tamanio_pagina);

	PedidoInicializar paquete_inicio = {
		.operacion = MEM_INICIALIZAR_PROGRAMA,
		.PID = PID,
		.paginas_codigo = paginas_codigo,		// Pongo cantidad de paginas por el enunciado
		.paginas_stack = STACK_SIZE
	};

	send(socket_memoria, &paquete_inicio, sizeof(paquete_inicio), 0);

	int respuesta;

	if (recv(socket_memoria, &respuesta, sizeof(respuesta), 0) <= 0) {
		terminar_kernel();
	}

	if (respuesta < 0) return respuesta; //Si no se pudieron reservar la cantidad de págs. para iniciar

	//Reservamos las páginas
	mem_asignar_paginas(PID, paquete_inicio.paginas_codigo + paquete_inicio.paginas_stack);

	//Vamos a escribir el código en las páginas de a pedazos
	PedidoEscritura paquete = {
		.operacion = MEM_ALMACENAR_BYTES,
		.PID = PID,
		.numero_pagina = 0,
		.offset = 0
	};

	int bytes = size;

	while (bytes > 0) {
		paquete.size = bytes > tamanio_pagina ? tamanio_pagina : bytes;
		//I had to do it again v:
		bytes -= paquete.size;

		respuesta = mem_escribir_bytes(paquete.PID, paquete.numero_pagina, 0, paquete.size, datos+paquete.offset);

		paquete.offset += paquete.size;
		paquete.numero_pagina++;
	}

	return respuesta;
}

char *mem_leer_bytes(int PID, int pagina, off_t offset, size_t size) {
	PedidoLectura paquete = {
			.operacion = MEM_SOLICITAR_BYTES,
			.PID = PID,
			.numero_pagina = pagina,
			.offset = offset,
			.size = size
	};

	send(socket_memoria, &paquete, sizeof(paquete), 0);

	int respuesta;
	if (recv(socket_memoria, &respuesta, sizeof(respuesta), 0) <= 0) {
		terminar_kernel();
	}

	if (respuesta < 0) return NULL;

	char *datos = malloc(size);

	if (recv(socket_memoria, datos, size, 0) <= 0) {
		terminar_kernel();
	}

	return datos;
}

int mem_escribir_bytes(int PID, int pagina, off_t offset, size_t size, void *buffer) {
	PedidoEscritura paquete = {
			.operacion = MEM_ALMACENAR_BYTES,
			.PID = PID,
			.numero_pagina = pagina,
			.offset = offset,
			.size = size
	};


	send(socket_memoria, &paquete, sizeof(paquete), 0);
	send(socket_memoria, buffer, size, 0);

	int respuesta;
	if (recv(socket_memoria, &respuesta, sizeof(respuesta), 0) <= 0) {
		terminar_kernel();
	}

	return respuesta;
}

int mem_asignar_paginas(int PID, int paginas) {
	PedidoAsignacion paquete = {
			.operacion = MEM_ASIGNAR_PAGINAS,
			.PID = PID,
			.paginas = paginas
	};

	send(socket_memoria, &paquete, sizeof(paquete), 0);

	int respuesta;
	if (recv(socket_memoria, &respuesta, sizeof(respuesta), 0) <= 0) {
		terminar_kernel();
	}

	return respuesta;
}

int mem_liberar_pagina(int PID, int numero_pagina) {
	PedidoLiberacion paquete = {
			.operacion = MEM_LIBERAR_PAGINA,
			.PID = PID,
			.numero_pagina = numero_pagina
	};

	send(socket_memoria, &paquete, sizeof(paquete), 0);

	int respuesta;
	if (recv(socket_memoria, &respuesta, sizeof(respuesta), 0) <= 0) {
		terminar_kernel();
	}

	return respuesta;
}

int mem_finalizar_programa(int PID) {
	PedidoFinalizacion paquete = {
			.operacion = MEM_FINALIZAR_PROGRAMA,
			.PID = PID
	};

	send(socket_memoria, &paquete, sizeof(paquete), 0);

	int respuesta;
	if (recv(socket_memoria, &respuesta, sizeof(respuesta), 0) <= 0) {
		terminar_kernel();
	}

	return respuesta;
}
