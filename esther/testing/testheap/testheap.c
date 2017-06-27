#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#define MARCOS 5
#define MARCO_SIZE 1024

#define RETARDO 100

#define LIBERAR_SEGURO 0

#if !LIBERAR_SEGURO
	#define LIBERAR(x) liberar_bloque(x)
#endif
#ifndef LIBERAR
	#define LIBERAR(x) liberar_bloque_seguro(x)
#endif

#define PACKED __attribute__((packed, aligned(1)))

typedef int t_puntero;

typedef struct {
	bool isfree;
	int size;
} PACKED HeapMetadata;

void init_memoria(void);
HeapMetadata leer_heap_metadata(off_t offset);
t_puntero alocar_bloque(int size);
bool escribir_heap_metadata(off_t offset, int new_size, int old_size);
void liberar_bloque(t_puntero direccion);
bool liberar_bloque_seguro(t_puntero direccion);
void ocupar_bloque(off_t offset);
void defragmentar(void);
void modificar_size_bloque(off_t offset, int size);

void *mem_leer(off_t offset, int size);
void mem_escribir(off_t offset, int size, void *data);

void imprimir_info_heap(void);

char *memoria;			// De un solo frame
int espacio_libre;		// Espacio total disponible

int main() {
	init_memoria();

	t_puntero a = alocar_bloque(10);
	t_puntero b = alocar_bloque(20);
	t_puntero c = alocar_bloque(25);
	t_puntero d = alocar_bloque(50);
	imprimir_info_heap();

	LIBERAR(c);					// Liberar 3er Bloque
	imprimir_info_heap();

	c = alocar_bloque(25);						// Vuelve a ocupar el 3er bloque
	imprimir_info_heap();

	LIBERAR(d);					// Libero 4to bloque
	d = alocar_bloque(35);						// Tendria un bloque de 35 otro de 10
	imprimir_info_heap();

	imprimir_info_heap();

	/* Libero los 3 primeros bloques, quedarian 2 bloques libres y uno ocupado */
	LIBERAR(a);
	LIBERAR(b);
	LIBERAR(c);
	imprimir_info_heap();

	LIBERAR(d);					// Se libera el ultimo bloque
	imprimir_info_heap();

	return EXIT_SUCCESS;
}

void init_memoria(void) {
	memoria = calloc(MARCOS, MARCO_SIZE);

	HeapMetadata *hm_inicial = (HeapMetadata *) memoria;
	hm_inicial->isfree = true;
	hm_inicial->size = MARCO_SIZE - sizeof(HeapMetadata);

	espacio_libre = MARCO_SIZE - sizeof(HeapMetadata);
}

HeapMetadata leer_heap_metadata(off_t offset) {
	HeapMetadata *p_hm;
	p_hm = mem_leer(offset, sizeof p_hm);

	HeapMetadata hm = *p_hm;
	free(p_hm);

	return hm;
}

/* malloc() */
t_puntero alocar_bloque(int size) {
	if (size > espacio_libre) {
		/* Pedir otra pagina */
		return -1;
	}

	int offset;
	HeapMetadata hm_libre;

	for (offset = 0; offset < MARCO_SIZE; offset += hm_libre.size + sizeof(HeapMetadata)) {
		hm_libre = leer_heap_metadata(offset);
		if (hm_libre.isfree) {
			// El bloque libre tiene justo el tamaño deseado
			if (hm_libre.size == size) {
				ocupar_bloque(offset);
				espacio_libre -= size;
			}

			else if (hm_libre.size > size + sizeof(HeapMetadata)
					&& !escribir_heap_metadata(offset, size, hm_libre.size)) {
				return -1;
			}

			return offset + sizeof(HeapMetadata);
		}
	}

	/* Pedir otra pagina */
	return -1;
}

/* free() */
void liberar_bloque(t_puntero direccion) {
	bool isfree = true;
	mem_escribir(direccion - sizeof(HeapMetadata), sizeof isfree, &isfree);

	defragmentar();
}


/* Esta chequea si la posicion del puntero es el inicio de un bloque reservado */
bool liberar_bloque_seguro(t_puntero direccion) {
	int i;
	int inicio_hm = direccion - sizeof(HeapMetadata);
	HeapMetadata hm;

	for (i = 0; i <= inicio_hm && i < MARCO_SIZE; i += hm.size + sizeof(HeapMetadata)) {
		hm = leer_heap_metadata(i);

		if (i == inicio_hm) {
			liberar_bloque(direccion);
			return true;
		}
	}

	return false;
}

/*
 * Partir el bloque en dos y ocupar la primera con tamaño = new_size.
 * Se pasa como parametro old_size para evitar otra lectura a memoria.
 */
bool escribir_heap_metadata(off_t offset, int new_size, int old_size) {
	if (offset + new_size + sizeof(HeapMetadata[2]) > MARCO_SIZE) {
		return false;
	}

	HeapMetadata hm_a_escribir;

	hm_a_escribir.isfree = false;
	hm_a_escribir.size = new_size;

	mem_escribir(offset, sizeof hm_a_escribir, &hm_a_escribir);

	offset += sizeof(HeapMetadata) + new_size;

	hm_a_escribir = leer_heap_metadata(offset);

	hm_a_escribir.isfree = true;
	hm_a_escribir.size = old_size - sizeof(HeapMetadata) - new_size;

	mem_escribir(offset, sizeof hm_a_escribir, &hm_a_escribir);

	espacio_libre -= new_size + sizeof(HeapMetadata);

	return true;
}

void modificar_size_bloque(off_t offset, int size) {
	memcpy(&memoria[offset + sizeof(bool)], &size, sizeof size);
}

void ocupar_bloque(off_t offset) {
	bool isfree = false;
	mem_escribir(offset, sizeof isfree, &isfree);
}

/* Deberia recibir tambien PID y pagina */
void *mem_leer(off_t offset, int size) {
	usleep(RETARDO * 1000);

	void *bytes = malloc(size);
	memcpy(bytes, &memoria[offset], size);

	return bytes;
}

/* Deberia recibir tambien PID y pagina */
void mem_escribir(off_t offset, int size, void *data) {
	usleep(RETARDO * 1000);

	memcpy(&memoria[offset], data, size);
}

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

void defragmentar(void) {
	off_t i, j;
	HeapMetadata hm, hm_j;

	espacio_libre = 0;		// Recalcula el espacio disponible

	for (i = 0; i < MARCO_SIZE; i += hm.size + sizeof(HeapMetadata)) {
		hm = leer_heap_metadata(i);

		if (hm.isfree) {
			int size = hm.size;

			for (j = i + size + sizeof hm;
					j < MARCO_SIZE && (hm_j = leer_heap_metadata(j)).isfree;
					j += hm_j.size + sizeof(HeapMetadata)) {
				size += sizeof hm_j + hm_j.size;
			}

			if (size != hm.size) {
				modificar_size_bloque(i, size);
				hm.size = size;
			}

			espacio_libre += hm.size;
		}
	}
}
