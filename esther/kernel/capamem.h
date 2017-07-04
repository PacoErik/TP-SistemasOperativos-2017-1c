#ifndef CAPAMEM_H_
#define CAPAMEM_H_

	#include <stdio.h>
	#include <stdbool.h>
	#include <stdlib.h>

	#include "kernel.h"
	#include "op_memoria.h"

	#define MARCO_SIZE marco_size()

	#define PACKED __attribute__((packed, aligned(1)))

	typedef struct {
		bool isfree;
		int size;
	} PACKED HeapMetadata;

	int asignar_pagina_heap(int PID);

	int liberar_pagina_heap(t_list *paginas_heap, int PID, int nro_pagina);

	/* RESERVAR */
	t_puntero alocar_bloque(int PID, int size);

	/* LIBERAR */
	bool liberar_bloque(int PID, t_puntero direccion);

#endif /* CAPAMEM_H_ */
