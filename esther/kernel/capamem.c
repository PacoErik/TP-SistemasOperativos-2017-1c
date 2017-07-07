#include "capamem.h"

static bool leer_heap_metadata(int PID, int pagina, off_t offset, HeapMetadata *hm_leido);
static bool escribir_heap_metadata(int PID, int pagina, off_t offset, int new_size, int old_size);

static int ocupar_bloque(int PID, int pagina, off_t offset);
static bool defragmentar_pagina(int PID, int pagina);
static bool modificar_size_bloque(int PID, int pagina, off_t offset, int newsize);

static inline t_puntero _direccion_pag_offset(int pagina, off_t offset);

/*
 * Devuelve numero de pagina agregada.
 */
int asignar_pagina_heap(int PID) {
	int nro_pagina = agregar_pagina_heap(PID);

	if (nro_pagina == -1) {
		/* Muy raro que pase eso, PID deberia estar incorrecto */
		return EXCEPCION_KERNEL;
	}

	int ret = mem_asignar_paginas(PID, 1);

	if (ret < 0) {
		return ret;		// Devuelve error code
	}

	return nro_pagina;
}

int liberar_pagina_heap(t_list *paginas_heap, int PID, int nro_pagina) {
	eliminar_pagina_heap(paginas_heap, nro_pagina);
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
			return FALLO_DE_SEGMENTO;
		}

		if (hm_libre.isfree) {
			// El bloque libre tiene justo el tamaño deseado
			if (hm_libre.size == size) {
				int ret = ocupar_bloque(PID, nro_pagina, offset);
				pagina->espacio -= size;

				return ret < 0 ? ret : offset;
			}

			if (hm_libre.size >= size + sizeof(HeapMetadata)) {
				return escribir_heap_metadata(PID, nro_pagina, offset, size, hm_libre.size)
							? offset + sizeof(HeapMetadata)
							: FALLO_DE_SEGMENTO;
			}

			/* Se le resta el tamanio del bloque libre cuando lo saltea porque ya no puede utilizarlo. */
			espacio_disponible -= hm_libre.size;
		}
	}

	return -1;
}

t_puntero alocar_bloque(int PID, int size) {
	if (size > MARCO_SIZE - sizeof(HeapMetadata[2])) return INTENTO_RESERVAR_MAS_MEMORIA_QUE_PAGINA;

	t_puntero retorno = -1;

	void _intentar_alocar(void *e) {
		Pagina_Heap *pagina = e;
		if (pagina->espacio >= size && retorno == -1) {
			int offset = alocar_bloque_en_pagina(PID, pagina->nro_pagina, size);

			if (offset >= 0)
				retorno = _direccion_pag_offset(pagina->nro_pagina, offset);

			if (offset < -1)
				retorno = offset;
		}
	}

	list_iterate(lista_paginas_heap_proceso(PID), &_intentar_alocar);

	if (retorno != -1) return retorno;

	int nro_pagina = asignar_pagina_heap(PID);
	if (nro_pagina <= 0) {
		return nro_pagina;		// En realidad es el error code.
	}

	Proceso *proceso = (Proceso*)proceso_segun_pid(PID);
	proceso->cantidad_paginas_heap++;

	HeapMetadata inicial = {
			.isfree = true,
			.size = MARCO_SIZE - sizeof(inicial)
	};

	mem_escribir_bytes(PID, nro_pagina, 0, sizeof(inicial), &inicial);

	int offset = alocar_bloque_en_pagina(PID, nro_pagina, size);

	if (offset >= 0) {
		return _direccion_pag_offset(nro_pagina, offset);
	}

	return offset;
}

bool liberar_bloque(int PID, t_puntero direccion) {
	int pagina = direccion / MARCO_SIZE;
	int offset = direccion % MARCO_SIZE;

	if (offset - sizeof(HeapMetadata) < 0) {
		return false;
	}

	bool isfree = true;
	if (mem_escribir_bytes(PID, pagina, offset - sizeof(HeapMetadata), sizeof isfree, &isfree) < 0) return false;

	if (!defragmentar_pagina(PID, pagina)) return false;

	return true;
}

int liberar_bloque_pro(int PID, t_puntero direccion) {
	int pagina = direccion / MARCO_SIZE;
	int offset = direccion % MARCO_SIZE;
	Proceso *proceso = (Proceso*)proceso_segun_pid(PID);
	int offset_snd;

	Pagina_Heap* pagina_heap = pagina_heap_proceso(PID, pagina);

	if (pagina_heap == NULL) return PUNTERO_INVALIDO;

	HeapMetadata fst_hm;
	HeapMetadata snd_hm;

	if (!leer_heap_metadata(PID, pagina, 0, &fst_hm)) return PUNTERO_INVALIDO;

	offset_snd = sizeof(HeapMetadata) + fst_hm.size;

	while (offset_snd + sizeof(HeapMetadata) <= offset || offset_snd - fst_hm.size == offset) {
		if (!leer_heap_metadata(PID, pagina, offset_snd, &snd_hm)) return PUNTERO_INVALIDO;

		if (offset_snd + sizeof(HeapMetadata) == offset) {
			if (!snd_hm.isfree) {
				if (fst_hm.isfree) {
					pagina_heap->espacio += sizeof(HeapMetadata) + snd_hm.size;
					proceso->bytes_liberados += snd_hm.size;
					logear_info("1 - Bloque de %d compactado con bloque de %d", fst_hm.size, snd_hm.size);
					fst_hm.size += sizeof(HeapMetadata) + snd_hm.size;
					if (fst_hm.size == MARCO_SIZE - sizeof(HeapMetadata)) return liberar_pagina_heap(lista_paginas_heap_proceso(PID), PID, pagina);

					return mem_escribir_bytes(PID, pagina, offset_snd - fst_hm.size + snd_hm.size, sizeof(HeapMetadata), &fst_hm);
				} else {
					fst_hm = snd_hm;
					offset_snd += sizeof(HeapMetadata) + snd_hm.size;

					if (!leer_heap_metadata(PID, pagina, offset_snd, &snd_hm)) return PUNTERO_INVALIDO;
					proceso->bytes_liberados += fst_hm.size;
					fst_hm.isfree = true;
					if (!snd_hm.isfree) {
						pagina_heap->espacio += fst_hm.size;
					} else {
						pagina_heap->espacio += fst_hm.size + sizeof(HeapMetadata);
						logear_info("2 - Bloque de %d compactado con bloque de %d", fst_hm.size, snd_hm.size);
						fst_hm.size += sizeof(HeapMetadata) + snd_hm.size;

						if (fst_hm.size == MARCO_SIZE - sizeof(HeapMetadata)) return liberar_pagina_heap(lista_paginas_heap_proceso(PID), PID, pagina);

						return mem_escribir_bytes(PID, pagina, offset_snd - fst_hm.size + snd_hm.size, sizeof(HeapMetadata), &fst_hm);
					}
					return mem_escribir_bytes(PID, pagina, offset_snd - fst_hm.size - sizeof(HeapMetadata), sizeof(HeapMetadata), &fst_hm);
				}
			}
			return PUNTERO_INVALIDO;
		}

		if (offset_snd - fst_hm.size == offset) {
			if (!fst_hm.isfree) {
				fst_hm.isfree = true;
				if (snd_hm.isfree) {
					pagina_heap->espacio += fst_hm.size + sizeof(HeapMetadata);
					proceso->bytes_liberados += fst_hm.size;
					logear_info("3 - Bloque de %d compactado con bloque de %d", fst_hm.size, snd_hm.size);
					fst_hm.size += sizeof(HeapMetadata) + snd_hm.size;

					if (fst_hm.size == MARCO_SIZE - sizeof(HeapMetadata)) return liberar_pagina_heap(lista_paginas_heap_proceso(PID), PID, pagina);

					return mem_escribir_bytes(PID, pagina, offset_snd - fst_hm.size + snd_hm.size, sizeof(HeapMetadata), &fst_hm);
				} else {
					pagina_heap->espacio += fst_hm.size;
					proceso->bytes_liberados += fst_hm.size;
				}
				return mem_escribir_bytes(PID, pagina, offset_snd - fst_hm.size - sizeof(HeapMetadata), sizeof(HeapMetadata), &fst_hm);
			}
			return PUNTERO_INVALIDO;
		}

		offset_snd += sizeof(HeapMetadata) + snd_hm.size;
		fst_hm = snd_hm;
	}

	return PUNTERO_INVALIDO;
}

int liberar_bloque_seguro(int PID, t_puntero direccion) {
	int pagina = direccion / MARCO_SIZE;
	int offset = direccion % MARCO_SIZE;

	int i;
	int inicio_hm = direccion - sizeof(HeapMetadata);
	HeapMetadata hm;

	for (i = 0; i <= inicio_hm && i < MARCO_SIZE; i += hm.size + sizeof(HeapMetadata)) {
		if (!leer_heap_metadata(PID, pagina, i, &hm)) return FALLO_DE_SEGMENTO;

		if (i == inicio_hm) {
			if (!liberar_bloque(PID, direccion)) return FALLO_DE_SEGMENTO;

			return hm.size;
		}
	}

	return PUNTERO_INVALIDO;
}

/*
 * Modifica el tamanio del bloque (size = newsize).
 */
static bool modificar_size_bloque(int PID, int pagina, off_t offset, int newsize) {
	if (mem_escribir_bytes(PID, pagina, offset + sizeof(bool), sizeof newsize, &newsize) < 0) return false;

	return true;
}

/*
 * Marcar el bloque como ocupado (isfree = false).
 */
static int ocupar_bloque(int PID, int pagina, off_t offset) {
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

	if (hm_leido->size > MARCO_SIZE - sizeof(HeapMetadata) || hm_leido->size < 0) { //Metadata corrupto o incorrecto
		return false;
	}

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

	if (mem_escribir_bytes(PID, pagina, offset, sizeof(HeapMetadata), &hm_a_escribir) < 0) return false;

	offset += sizeof(HeapMetadata) + new_size;

	hm_a_escribir.isfree = true;
	hm_a_escribir.size = old_size - sizeof(HeapMetadata) - new_size;

	if (mem_escribir_bytes(PID, pagina, offset, sizeof(HeapMetadata), &hm_a_escribir) < 0) return false;

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

static bool defragmentar_pagina(int PID, int pagina) {
	off_t i, j;
	size_t size;			// Tamanio del ultimo hm leido
	HeapMetadata hm, hm_j;

	pagina_heap_proceso(PID, pagina)->espacio = 0;		// Recalcula el espacio disponible

	for (i = 0; i < MARCO_SIZE; i += hm.size + sizeof(HeapMetadata)) {
		if (!leer_heap_metadata(PID, pagina, i, &hm)) return false;

		if (hm.isfree) {
			int size = hm.size;

			for (j = i + size + sizeof hm;
					j <= MARCO_SIZE - sizeof hm && leer_heap_metadata(PID, pagina, j, &hm_j) && hm_j.isfree;
					j += hm_j.size + sizeof(HeapMetadata))
			{
				size += sizeof hm_j + hm_j.size;
			}

			if (size != hm.size) {
				if (!modificar_size_bloque(PID, pagina, i, size)) return false;
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
		if (liberar_pagina_heap(lista_paginas_heap_proceso(PID), PID, pagina) < 0) return false;
	}

	return true;
}

inline t_puntero _direccion_pag_offset(int pagina, off_t offset) {
	return pagina * MARCO_SIZE + offset;
}
