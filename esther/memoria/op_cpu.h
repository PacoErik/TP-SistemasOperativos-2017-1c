#ifndef OP_CPU_H_
#define OP_CPU_H_

	typedef struct posicionDeMemoriaAPedir {
		int processID;
		int numero_pagina;
		int offset;
		int size;
	} posicion_memoria_pedida;

	typedef struct posicionDeMemoriaVariable {
		int pid;
		int posicion_numero;
		int numero_pagina;
		int start;
		int offset;
	} posicion_memoria_variable;

int cpu_procesar_operacion	(int);

#endif /* OP_CPU_H_ */
