#include "capamem.h"

static bool leer_heap_metadata(int PID, int pagina, off_t offset, HeapMetadata *hm_leido);
static bool escribir_heap_metadata(int PID, int pagina, off_t offset, int new_size, int old_size);

static bool ocupar_bloque(int PID, int pagina, off_t offset);
static void defragmentar_pagina(int PID, int pagina);
static void modificar_size_bloque(int PID, int pagina, off_t offset, int newsize);

static inline t_puntero _direccion_pag_offset(int pagina, off_t offset);

/*
 * Devuelve numero de pagina agregada.
 */
int asignar_pagina_heap(int PID) {
	int nro_pagina = agregar_pagina_heap(PID);

	if (nro_pagina == -1) {
		/* Muy raro que pase eso, PID deberia estar incorrecto */
		return -1;
	}

	int ret = mem_asignar_paginas(PID, 1);

	if (ret < 0) {
		return ret;		// Devuelve error code
	}

	return nro_pagina;
}

int liberar_pagina_heap(int PID, int nro_pagina) {
	eliminar_pagina_heap(PID, nro_pagina);
	return mem_liberar_pagina(PID, nro_pagina);
}

/*
 * Devuelve el offset en la pagina del bloque asignado.
 * Devuelve -1 si no pudo asignarla.
 * Devuelve -2 si hubo error al leer/escribir el heap metadata.
 */
int alocar_bloque_en_pagina(int PID, int nro_pagina, int size) {
	int offset;
	HeapMetadata hm_libre;

	Pagina_Heap *pagina = pagina_heap_proceso(PID, nro_pagina);
	int espacio_disponible = pagina->espacio;

	for (offset = 0;
			offset < MARCO_SIZE && espacio_disponible >= size;
			offset += hm_libre.size + sizeof(HeapMetadata))
	{
		if (!leer_heap_metadata(PID, nro_pagina, offset, &hm_libre)) {
			return -2;
		}

		if (hm_libre.isfree) {
			// El bloque libre tiene justo el tamaño deseado
			if (hm_libre.size == size) {
				bool ret = ocupar_bloque(PID, nro_pagina, offset);
				pagina->espacio -= size;

				return ret ? _direccion_pag_offset(nro_pagina, offset + sizeof(HeapMetadata))
							: -2;
			}

			if (hm_libre.size >= size + sizeof(HeapMetadata)) {
				return escribir_heap_metadata(PID, nro_pagina, offset, size, hm_libre.size)
							? _direccion_pag_offset(nro_pagina, offset + sizeof(HeapMetadata))
							: -2;
			}

			/* Se le resta el tamanio del bloque libre cuando lo saltea porque ya no puede utilizarlo. */
			espacio_disponible -= hm_libre.size;
		}
	}

	return -1;
}

t_puntero alocar_bloque(int PID, int size) {
	bool _hay_espacio_suficiente(void *e) {
		return e != NULL
				? ((Pagina_Heap *)e)->espacio >= size
				: false;
	}

	t_list *paginas_disponibles = list_filter(lista_paginas_heap_proceso(PID), _hay_espacio_suficiente);

	int i;
	for (i = 0; i < list_size(paginas_disponibles); i++) {
		Pagina_Heap *pagina = list_get(paginas_disponibles, i);
		int offset = alocar_bloque_en_pagina(PID, pagina->nro_pagina, size);

		if (offset >= 0) {
			return _direccion_pag_offset(pagina->nro_pagina, offset);
		}

		/* TODO: Manejar el error si offset == -2 */
	}

	int nro_pagina = asignar_pagina_heap(PID);
	if (nro_pagina <= 0) {
		return nro_pagina;		// En realidad es el error code.
	}

	int offset = alocar_bloque_en_pagina(PID, nro_pagina, size);

	if (offset >= 0) {
		return _direccion_pag_offset(nro_pagina, offset);
	}

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
static void modificar_size_bloque(int PID, int pagina, off_t offset, int newsize) {
	mem_escribir_bytes(PID, pagina, offset + sizeof(bool), sizeof newsize, &newsize);
}

/*
 * Marcar el bloque como ocupado (isfree = false).
 */
static bool ocupar_bloque(int PID, int pagina, off_t offset) {
	bool isfree = false;
	return mem_escribir_bytes(PID, pagina, offset, sizeof isfree, &isfree);
}

/*
 * Recibe el heap metadata por referencia.
 * Devuelve true/false para indicar si pudo leer el heap metadata.
 */
static bool leer_heap_metadata(int PID, int pagina, off_t offset, HeapMetadata *hm_leido) {
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
static bool escribir_heap_metadata(int PID, int pagina, off_t offset, int new_size, int old_size) {
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

	pagina_heap_proceso(PID, pagina)->espacio -= new_size + sizeof(HeapMetadata);

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

static void defragmentar_pagina(int PID, int pagina) {
	off_t i, j;
	size_t size;			// Tamanio del ultimo hm leido
	HeapMetadata hm, hm_j;

	pagina_heap_proceso(PID, pagina)->espacio = 0;		// Recalcula el espacio disponible

	for (i = 0; i < MARCO_SIZE; i += hm.size + sizeof(HeapMetadata)) {
		leer_heap_metadata(PID, pagina, i, &hm);

		if (hm.isfree) {
			int size = hm.size;

			for (j = i + size + sizeof hm;
					j < MARCO_SIZE && leer_heap_metadata(PID, pagina, j, &hm_j) && hm_j.isfree;
					j += hm_j.size + sizeof(HeapMetadata))
			{
				size += sizeof hm_j + hm_j.size;
			}

			if (size != hm.size) {
				modificar_size_bloque(PID, pagina, i, size);
				hm.size = size;
			}

			pagina_heap_proceso(PID, pagina)->espacio += hm.size;
		}
	}

	/* Hay un unico bloque porque solo hubo una iteracion en el for grande,
	 * el unico caso en el que deberia pasar eso es cuando queda un solo bloque
	 * libre, ya que al tener un bloque ocupado tendria al menos 2 bloques, uno
	 * ocupado, otro libre al final de tod o.
	 */
	if (i == 0) {
		liberar_pagina_heap(PID, pagina);
	}
}

inline t_puntero _direccion_pag_offset(int pagina, off_t offset) {
	return pagina * MARCO_SIZE + offset;
}
