#ifndef OP_KERNEL_H_
#define OP_KERNEL_H_

	#include <stdio.h>
	#include <stddef.h>
	#include <stdlib.h>
	#include <qepd/qepd.h>

	/* Codigos de Operacion de Kernel */
	typedef enum PACKED {
		MEM_MENSAJE,
		MEM_INICIALIZAR_PROGRAMA,
		MEM_ASIGNAR_PAGINAS,
		MEM_FINALIZAR_PROGRAMA,
		MEM_SOLICITAR_BYTES,
		MEM_ALMACENAR_BYTES,
		MEM_LIBERAR_PAGINA
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

	typedef struct {
		int PID;
		int numero_pagina;
		int offset;
		int size;
	} PACKED PedidoLectura;

	typedef struct {
		int PID;
		int numero_pagina;
		int offset;
		int size;
		void *datos;
	} PACKED PedidoEscritura;

	typedef struct {
		int PID;
		int numero_pagina;
	} PACKED PedidoLiberacion;

	int socket_kernel;

	/**
	 * Recibe un codigo de operacion enviado por kernel
	 * y ejecuta la operacion correspondiente.
	 */
	int kernel_procesar_operacion();

	/**
	 * Recibir un mensaje del kernel.
	 */
	int kernel_mensaje();

	/**
	 * Inicializar programa
	 */
	int kernel_inicializar_programa();

	/**
	 * Asignar Páginas a Proceso
	 */
	int kernel_asignar_paginas();

	/**
	 * Finalizar programa
	 */
	int kernel_finalizar_programa();

	/**
	 * Leer bytes de memoria
	 */
	int kernel_solicitar_bytes();

	/**
	 * Escribir bytes en memoria
	 */
	int kernel_almacenar_bytes();

	/**
	 * Liberar pagina del proceso
	 */
	int kernel_liberar_pagina();


#endif /* OP_KERNEL_H_ */
