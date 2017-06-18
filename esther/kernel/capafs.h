#ifndef CAPAFS_H_
#define CAPAFS_H_

	#include <stdio.h>
	#include <stddef.h>
	#include <stdlib.h>
	#include <string.h>

	#include "commons/collections/list.h"
	#include "qepd/qepd.h"

	typedef t_list* global_file_table;
	typedef t_list* process_file_table;

	typedef int file_descriptor_t;
	typedef int cursor_t;

	extern char IP_FS[16];
	extern int PUERTO_FS;
	extern global_file_table tabla_archivos_global;

	#define FD_INICIAL 3

	/*
	 * Estructura de un elemento de la tabla global de archivos.
	 */
	typedef struct {
		char *path;
		int cantidad;
	} info_gft;

	typedef struct {
		bool lectura;
		bool escritura;
		bool creacion;
	} flags_t;

	/*
	 * Estructura de un elemento de la tabla de archivos de un proceso.
	 */
	typedef struct {
		flags_t banderas;
		cursor_t posicion;
		file_descriptor_t fd_global;
	} info_pft;

	/*
	 * Operaciones del File System
	 */

	int fs_conectar(void);

	file_descriptor_t fs_abrir_archivo(int PID, char *path, flags_t banderas);

	int fs_borrar_archivo(int PID, file_descriptor_t fd);

	void *fs_leer_archivo(int PID, file_descriptor_t fd, size_t tamanio, int *errorcode);

	bool fs_cerrar_archivo(int PID, file_descriptor_t fd);

	int fs_escribir_archivo(int PID, file_descriptor_t fd, void *datos, size_t tamanio);

	/*
	 * Inicializar la tabla global de archivos y la lista de tablas para cada proceso.
	 */
	void init_tabla_archivos(void);

	/*
	 * Elimina la tabla de archivos de un proceso.
	 */
	void destroy_tabla_archivos_proceso(process_file_table tabla);

#endif /* CAPAFS_H_ */
