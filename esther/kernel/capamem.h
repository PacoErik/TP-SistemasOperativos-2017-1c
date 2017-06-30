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

#endif /* CAPAMEM_H_ */
