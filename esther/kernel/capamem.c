#include "capamem.h"

bool leer_heap_metadata(int PID, int pagina, off_t offset, HeapMetadata *hm_leido);
t_puntero alocar_bloque(int PID, int size);
bool escribir_heap_metadata(int PID, int pagina, off_t offset, int new_size, int old_size);
bool liberar_bloque(int PID, t_puntero direccion);
bool liberar_bloque_seguro(int PID, t_puntero direccion);
void ocupar_bloque(int PID, int pagina, off_t offset);
void defragmentar_pagina(int PID, int pagina);
void modificar_size_bloque(int PID, int pagina, off_t offset, int newsize);

//void imprimir_info_heap(void);

int espacio_libre;		// Espacio total disponible

inline t_puntero _direccion_pag_offset(int pagina, off_t offset);

/*
 * TODO:
 * 	asignar_pagina_heap(): llama a mem_asignar_paginas() y modifica PCB
 * 	liberar_pagina_heap()
 */

t_puntero alocar_bloque(int PID, int size) {
	if (size > espacio_libre) {
		/* Pedir otra pagina */
		return -1;
	}

	int pagina; /* TODO: obtener pagina heap */

	int offset;
	HeapMetadata hm_libre;

	for (offset = 0; offset < MARCO_SIZE; offset += hm_libre.size + sizeof(HeapMetadata)) {
		leer_heap_metadata(PID, pagina, offset, &hm_libre);
		if (hm_libre.isfree) {
			// El bloque libre tiene justo el tamaño deseado
			if (hm_libre.size == size) {
				ocupar_bloque(PID, pagina, offset);
				espacio_libre -= size;
			}

			else if (hm_libre.size > size + sizeof(HeapMetadata)
					&& !escribir_heap_metadata(PID, pagina, offset, size, hm_libre.size)) {
				return -1;
			}

			return _direccion_pag_offset(pagina, offset + sizeof(HeapMetadata));
		}
	}

	/* Pedir otra pagina */
	return -1;
}

bool liberar_bloque(int PID, t_puntero direccion) {
	int pagina = direccion / MARCO_SIZE;
	int offset = direccion % MARCO_SIZE;

	if (offset - sizeof(HeapMetadata) < 0) {
		return false;
	}

	bool isfree = true;
	mem_escribir_bytes(PID, pagina, offset - sizeof(HeapMetadata), sizeof isfree, &isfree);

	defragmentar_pagina(PID, pagina);

	return true;
}


/* Esta chequea si la posicion del puntero es el inicio de un bloque reservado */
bool liberar_bloque_seguro(int PID, t_puntero direccion) {
	int pagina = direccion / MARCO_SIZE;
	int offset = direccion % MARCO_SIZE;

	int i;
	int inicio_hm = direccion - sizeof(HeapMetadata);
	HeapMetadata hm;

	for (i = 0; i <= inicio_hm && i < MARCO_SIZE; i += hm.size + sizeof(HeapMetadata)) {
		leer_heap_metadata(PID, pagina, i, &hm);

		if (i == inicio_hm) {
			liberar_bloque(PID, direccion);
			return true;
		}
	}

	return false;
}

/*
 * Modifica el tamanio del bloque (size = newsize).
 */
void modificar_size_bloque(int PID, int pagina, off_t offset, int newsize) {
	mem_escribir_bytes(PID, pagina, offset + sizeof(bool), sizeof newsize, &newsize);
}

/*
 * Marcar el bloque como ocupado (isfree = false).
 */
void ocupar_bloque(int PID, int pagina, off_t offset) {
	bool isfree = false;
	mem_escribir_bytes(PID, pagina, offset, sizeof isfree, &isfree);
}

bool leer_heap_metadata(int PID, int pagina, off_t offset, HeapMetadata *hm_leido) {
	void *data = mem_leer_bytes(PID, pagina, offset, sizeof(HeapMetadata));
	if (data == NULL) {
		return false;
	}

	memcpy(hm_leido, data, sizeof(HeapMetadata));
	free(data);

	return true;
}

/*
 * Partir el bloque en dos y ocupar la primera con tamaño = new_size.
 * Se pasa como parametro old_size para evitar otra lectura a memoria.
 */
bool escribir_heap_metadata(int PID, int pagina, off_t offset, int new_size, int old_size) {
	if (offset + new_size + sizeof(HeapMetadata[2]) > MARCO_SIZE) {
		return false;
	}

	if (new_size > old_size) { logear_error("BUGGEADO", true); }

	HeapMetadata hm_a_escribir;

	hm_a_escribir.isfree = false;
	hm_a_escribir.size = new_size;

	mem_escribir_bytes(PID, pagina, offset, sizeof(HeapMetadata), &hm_a_escribir);

	offset += sizeof(HeapMetadata) + new_size;

	hm_a_escribir.isfree = true;
	hm_a_escribir.size = old_size - sizeof(HeapMetadata) - new_size;

	mem_escribir_bytes(PID, pagina, offset, sizeof(HeapMetadata), &hm_a_escribir);

	espacio_libre -= new_size + sizeof(HeapMetadata);

	return true;
}

/*
void imprimir_info_heap(void) {
	int i_bloque;
	off_t offset;
	HeapMetadata hm;

	printf("--------------------\n");
	printf("Espacio total disponible: %d\n", espacio_libre);

	for (i_bloque = 0, offset = 0; offset < MARCO_SIZE;
			offset += hm.size + sizeof(HeapMetadata), i_bloque++) {
		hm = leer_heap_metadata(offset);

		printf("Bloque %d:\n"
					"\tEstado: %s\n"
					"\tTamaño: %d\n",
				i_bloque, hm.isfree ? "Libre" : "Ocupado", hm.size);
	}
}
*/

void defragmentar_pagina(int PID, int pagina) {
	off_t i, j;
	size_t size;			// Tamanio del ultimo hm leido
	HeapMetadata hm, hm_j;

	espacio_libre = 0;		// Recalcula el espacio disponible

	for (i = 0; i < MARCO_SIZE; i += hm.size + sizeof(HeapMetadata)) {
		leer_heap_metadata(PID, pagina, i, &hm);

		if (hm.isfree) {
			int size = hm.size;

			for (j = i + size + sizeof hm;
					j < MARCO_SIZE && leer_heap_metadata(PID, pagina, j, &hm_j) && hm_j.isfree;
					j += hm_j.size + sizeof(HeapMetadata)) {
				size += sizeof hm_j + hm_j.size;
			}

			if (size != hm.size) {
				modificar_size_bloque(PID, pagina, i, size);
				hm.size = size;
			}

			espacio_libre += hm.size;
		}
	}
}

inline t_puntero _direccion_pag_offset(int pagina, off_t offset) {
	return pagina * MARCO_SIZE + offset;
}
