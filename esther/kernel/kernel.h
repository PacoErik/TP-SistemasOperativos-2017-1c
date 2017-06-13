#ifndef KERNEL_H_
#define KERNEL_H_

	#include "commons/collections/list.h"
	#include "parser/metadata_program.h"

	typedef t_list* process_file_table;

	typedef struct PCB {
		int pid;
		int program_counter;
		int cantidad_paginas_codigo;

		t_size cantidad_instrucciones;
		t_intructions *instrucciones_serializado;

		t_size etiquetas_size;
		char *etiquetas;

		int	puntero_stack;
		t_list *indice_stack;

		process_file_table tabla_archivos;

		int exit_code;
	} PCB;

	typedef struct Proceso {
		int consola; //Socket de la consola que envio este proceso
		int cantidad_rafagas;
		int cantidad_paginas_heap;
		int cantidad_alocar;
		int bytes_alocados;
		int cantidad_liberar;
		int bytes_liberados;
		int cantidad_syscalls; //sí, me encantan los int
		char* codigo;
		int estado;
		PCB *pcb;
	} Proceso;

	typedef t_list listaProcesos;

	extern listaProcesos *procesos; //Dentro van a estar los procesos en estado READY/EXEC/BLOCKED

#endif /* KERNEL_H_ */
