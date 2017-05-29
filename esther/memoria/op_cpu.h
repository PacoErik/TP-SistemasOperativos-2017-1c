#ifndef OP_CPU_H_
#define OP_CPU_H_

	#include <stdio.h>
	#include <stddef.h>
	#include <stdlib.h>
	#include "qepd/qepd.h"
	typedef u_int32_t t_puntero;

#define PACKED __attribute__((packed, aligned(1)))



	typedef struct posicionDeMemoriaVariable {
		int processID;
		t_puntero posicionNumero;
		int numeroDePagina;
		int start;
		int offset;
	} posicionDeMemoriaVariable;

int cpu_processar_operacion		(int);

int cpu_obtener_variable		(int);

int buscarEnMemoriaYDevolver	(posicionDeMemoriaVariable);

void enviarDesdeCache			(int);








#endif /* OP_CPU_H_ */
