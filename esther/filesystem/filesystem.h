#ifndef FILESYSTEM_H_
#define FILESYSTEM_H_

	#define _XOPEN_SOURCE 500

	#include <stdio.h>
	#include <string.h>
	#include <errno.h>
	#include <sys/stat.h>
	#include <ftw.h>
	#include <libgen.h>

	#include "commons/bitarray.h"

	#include "qepd/qepd.h"

	#define ROUNDUP(x,y) ((x - 1) / y + 1)			// Redondear hacia arriba
	#define CANTIDAD_BLOQUES_ARCHIVO(FILE_SIZE, BLOCK_SIZE) ((FILE_SIZE > 0) ? ROUNDUP(FILE_SIZE, BLOCK_SIZE) : 1)

	typedef struct {
		int tamanio;
		int *bloques;
	} FileMetadata;

	typedef enum {
		FS_VALIDAR = 0,
		FS_CREAR,
		FS_ELIMINAR,
		FS_LEER,
		FS_ESCRIBIR
	} FS_Operacion;

	typedef struct {
		char *nombre;
		FS_Operacion op;
	} comando;

	/*
	 * Operaciones del File System
	 */

	bool validar_archivo(char*);
	bool crear_archivo(char*);
	bool eliminar_archivo(char*);
	char* leer_archivo(char*, off_t, size_t);
	bool escribir_archivo(char*, off_t, size_t, char*);

	/*
	 * Operaciones del Bitmap
	 */

	bool actualizar_archivo_bitmap(void);
	t_bitarray* crear_bitmap_vacio(void);
	t_bitarray* leer_bitmap(void);
	t_bitarray* limpiar_bitmap(void);
	void destruir_bitmap(void);

	/*
	 * Funciones para Bloques
	 */

	FILE* archivo_bloque_r(int);
	FILE* archivo_bloque_w(int);
	int* asignar_bloques(int, int**);
	void liberar_bloques(FileMetadata*);

	/*
	 * Funciones para archivo metadata
	 */

	bool actualizar_metadata_bitmap(char*, FileMetadata*);
	void destruir_metadata_archivo(FileMetadata*);
	void leer_metadata(void);
	FileMetadata* leer_metadata_archivo(char*);

	/*
	 * Conexion con Kernel
	 */

	void recibir_conexion_kernel(void);
	bool recibir_handshake(int socket);

	/*
	 * Comunicacion con Kernel
	 */

	void procesar_operacion_kernel(void);
	void kernel_validar(unsigned short bytes);
	void kernel_borrar(unsigned short bytes);
	void kernel_crear(unsigned short bytes);
	void kernel_leer(unsigned short bytes);
	void kernel_escribir(unsigned short bytes);

	/*
	 * Otros
	 */

	void establecer_configuracion(void);
	/*	void		leer_mensaje					(void);*/
	void interaccion_FS(void);
	void limpiar_directorio(char *ruta_dir);

	/*
	 * Funciones auxiliares
	 */

	char* _ruta_desde_punto_montaje(char*);
	char* _ruta_desde_archivos(char*);
	bool _crear_directorios(const char*);

#endif /* FILESYSTEM_H_ */
