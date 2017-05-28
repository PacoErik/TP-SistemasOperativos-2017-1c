#ifndef OP_KERNEL_H_
#define OP_KERNEL_H_

	#include <stdio.h>
	#include <stddef.h>
	#include <stdlib.h>

#define PACKED __attribute__((packed, aligned(1)))

	/* Codigos de Operacion de Kernel */
	typedef enum PACKED {
		MEM_MENSAJE,
		MEM_INICIALIZAR_PROGRAMA,
		MEM_ASIGNAR_PAGINAS,
		MEM_FINALIZAR_PROGRAMA
	} op_kernel;

	/* Mensaje de confirmacion */
	typedef enum PACKED {
		OK,
		ERROR
	} respuesta_op_kernel;

	/* Pedido de inicializacion de programa */
	typedef struct {
		int PID;
		int paginas_codigo;
		int paginas_stack;
		size_t bytes_datos;		// Cantidad de bytes del contenido de programa
		/* void *datos; */
	} PACKED PedidoInicializar;

	/* Pedido de asignacion de paginas */
	typedef struct {
		int PID;
		int paginas;
	} PACKED PedidoAsignacion;

	/* Finalizacion de programa */
	typedef struct {
		int PID;
	} PACKED PedidoFinalizacion;

#undef PACKED

	/**
	 * Recibe un codigo de operacion enviado por kernel
	 * y ejecuta la operacion correspondiente.
	 */
	int kernel_processar_operacion(int socket);

	/**
	 * Recibir un mensaje del kernel.
	 */
	int kernel_mensaje(int socket);

	/**
	 * Inicializar programa
	 */
	int kernel_inicializar_programa(int socket);

	/**
	 * Asignar Páginas a Proceso
	 */
	int kernel_asignar_paginas(int socket);

	/**
	 * Finalizar programa
	 */
	int kernel_finalizar_programa(int socket);


#endif /* OP_KERNEL_H_ */
