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
		short int lru;
		int contenidoDeLaPag; //Numero de byte a partir del cual empieza la pag en cuestion. Se llamo asi para respetar el enunciado
	} PACKED estructuraAdministrativa_cache;
	typedef struct estructuraAdministrativa {
		int frame;
		int pid;
		int pag;
	} PACKED estructuraAdministrativa;
	extern estructuraAdministrativa_cache *tablaAdministrativa_cache;
	extern estructuraAdministrativa *tablaAdministrativa;
	extern char *memoria_cache;
	extern char *memoria;
	extern int MARCO_SIZE;
	extern int MARCOS;
	extern short int RETARDO;


	typedef struct posicionDeMemoriaAPedir {
		int processID;
		int numero_pagina;
		int offset;
		int size;
	} posicionDeMemoriaAPedir;

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

int traducir_a_frame(int, int);






#endif /* OP_CPU_H_ */
