#include "op_memoria.h"

int socket_memoria;

int tamanio_pagina;

int STACK_SIZE;

char IP_MEMORIA[16];

int PUERTO_MEMORIA;

int mem_conectar(void) {
	struct sockaddr_in mem_info;

	mem_info.sin_family = AF_INET;
	mem_info.sin_addr.s_addr = inet_addr( (char*) IP_MEMORIA);
	mem_info.sin_port = htons(PUERTO_MEMORIA);

	socket_memoria = socket(AF_INET, SOCK_STREAM, 0);

	int ret = connect(socket_memoria, (struct sockaddr *) &mem_info, sizeof(mem_info));

	if (ret != 0) {
		close(socket_memoria);
		return 0;
	}

	handshake(socket_memoria, KERNEL);

	/* Recibe tamanio de pagina enviada por la memoria */
	ret = recv(socket_memoria, &tamanio_pagina, sizeof(int), 0);

	if (ret <= 0) {
		close(socket_memoria);
		return 0;
	}

	return 1;
}

void mem_mensaje(char *mensaje) {
	op_mem operacion = MEM_MENSAJE;
	int tamanio = strlen(mensaje) + 1;
	send(socket_memoria, &operacion, sizeof operacion, 0);
	send(socket_memoria, &tamanio, sizeof tamanio, 0);
	send(socket_memoria, mensaje, tamanio, 0);
}

#define DIVIDE_ROUNDUP(x,y) ((x - 1) / y + 1)

int mem_inicializar_programa(int PID, size_t size, void *datos) {



	PedidoInicializar paquete = {
		.operacion = MEM_INICIALIZAR_PROGRAMA,
		.PID = PID,
		.paginas_codigo = DIVIDE_ROUNDUP(size, tamanio_pagina),		// Pongo cantidad de paginas por el enunciado
		.paginas_stack = STACK_SIZE,
		.bytes_datos = size,
	};

	char *buffer = malloc(sizeof paquete + size);
	memcpy(buffer, &paquete, sizeof paquete);
	memcpy(buffer + sizeof paquete, datos, size);

	send(socket_memoria, buffer, sizeof paquete + size, 0);

	respuesta_op_mem respuesta;

	int ret = recv(socket_memoria, &respuesta, sizeof respuesta, 0);

	if (ret <= 0) {
		return -1;
	}

	switch (respuesta) {
	case OK:
		return 1;

	case ERROR:
		return 0;

	default:
		return -1;
	}
}

#undef DIVIDE_ROUNDUP

int mem_asignar_paginas(int PID, int paginas) {
	PedidoAsignacion paquete;

	paquete.operacion = MEM_ASIGNAR_PAGINAS;
	paquete.PID = PID;
	paquete.paginas = paginas;

	send(socket_memoria, &paquete, sizeof paquete, 0);

	/* TODO: Recibir confirmacion */

	return 1;
}

int mem_finalizar_programa(int PID) {
	PedidoFinalizacion paquete;

	paquete.operacion = MEM_FINALIZAR_PROGRAMA;
	paquete.PID = PID;

	send(socket_memoria, &paquete, sizeof paquete, 0);

	return 1;
}
