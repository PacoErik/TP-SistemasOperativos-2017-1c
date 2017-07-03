#ifndef KERNEL_H_
#define KERNEL_H_

	#include "commons/collections/list.h"
	#include "parser/metadata_program.h"

	#include "capamem.h"

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
		t_list *paginas_heap;
		PCB *pcb;
	} Proceso;

	typedef struct Pagina_Heap {
		int nro_pagina;
		int espacio;
	} Pagina_Heap;

	typedef t_list listaProcesos;

	extern listaProcesos *procesos; //Dentro van a estar los procesos en estado READY/EXEC/BLOCKED

	int marco_size(void);
	int agregar_pagina_heap(int PID);
	t_list *lista_paginas_heap_proceso(int PID);
	Pagina_Heap *pagina_heap_proceso(int PID, int nro_pagina);
	bool eliminar_pagina_heap(int PID, int nro_pagina);

#endif /* KERNEL_H_ */
