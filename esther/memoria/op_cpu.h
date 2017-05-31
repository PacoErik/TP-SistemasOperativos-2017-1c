#ifndef OP_CPU_H_
#define OP_CPU_H_

	#include <stdio.h>
	#include <stddef.h>
	#include <stdlib.h>
	#include "qepd/qepd.h"
	typedef u_int32_t t_puntero;
	typedef struct estructuraAdministrativa_cache {
		int pid;
		int pag;
		int contenidoDeLaPag; //Numero de byte a partir del cual empieza la pag en cuestion. Se llamo asi para respetar el enunciado
	} PACKED estructuraAdministrativa_cache;
	extern estructuraAdministrativa_cache *tablaAdministrativa_cache;
	extern char *memoria_cache;

	typedef struct posicionDeMemoriaVariable {
		int processID;
		t_puntero posicionNumero;
		int numeroDePagina;
		int start;
		int offset;
	} posicionDeMemoriaVariable;

int cpu_processar_operacion		(int);

int cpu_obtener_variable		(int);

int buscarEnMemoriaYDevolver	(posicionDeMemoriaVariable, int);

void enviarDesdeCache			(int, int, posicionDeMemoriaVariable);








#endif /* OP_CPU_H_ */
