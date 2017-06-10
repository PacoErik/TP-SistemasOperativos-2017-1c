#include "capafs.h"

static global_file_table tabla_archivos_global;
static process_file_table lista_tabla_archivos;

static file_descriptor_t		add_archivo_global			(char *path);
static file_descriptor_t		add_archivo_proceso			(int PID, flags_t banderas, file_descriptor_t fd_global);
static void					cerrar_fd_global				(file_descriptor_t fd_global);
static flags_t					get_banderas					(int PID, file_descriptor_t fd);
static info_pft *				get_fd_info					(int PID, file_descriptor_t fd);
static char *					get_fd_path					(int PID, file_descriptor_t fd);
static file_descriptor_t		get_global_fd					(char *path);
static cursor_t					get_posicion_cursor			(int PID, file_descriptor_t fd);
static process_file_table		get_tabla_archivos_proceso	(int PID);

void fs_abrir_archivo(int PID, char *path, flags_t banderas) {
	if (banderas.creacion) {
		/* FS: validar si existe el archivo */
	}

	file_descriptor_t fd_global = add_archivo_global(path);
	file_descriptor_t fd = add_archivo_proceso(PID, banderas, fd_global);

	/* Manda fd a cpu */
}

void fs_leer_archivo(int PID, file_descriptor_t fd, size_t tamanio) {
	if (!get_banderas(PID, fd).lectura) {
		/* Finalizar proceso */
	}

	char *path = get_fd_path(PID, fd);
	cursor_t desplazamiento = get_posicion_cursor(PID, fd);

	/* Pedido al File System */
	/* Envia datos a CPU */
}

void fs_cerrar_archivo(int PID, file_descriptor_t fd) {
	process_file_table tabla_archivos = get_tabla_archivos_proceso(PID);
	info_pft *info_fd = list_remove(tabla_archivos, fd - 1);

	if (info_fd == NULL) {
		/* Esta queriendo cerrar un archivo que no esta abierto?! */
	}

	cerrar_fd_global(info_fd->fd_global);
	free(info_fd);
}

void fs_escribir_archivo(int PID, file_descriptor_t fd, void *datos, size_t tamanio) {
	if (!get_banderas(PID, fd).escritura) {
		/* Finalizar proceso */
	}

	char *path = get_fd_path(PID, fd);
	cursor_t desplazamiento = get_posicion_cursor(PID, fd);

	/* Pedido al File System */
	/* Respuesta a CPU */
}

void init_tabla_archivos(void) {
	tabla_archivos_global = list_create();
	lista_tabla_archivos = list_create();
}

void destroy_tabla_archivos_proceso(int PID) {
	bool match_PID(void *element) {
		int _PID = ((list_file_table *)element)->PID;
		return _PID == PID;
	}

	list_file_table *list = list_remove_by_condition(lista_tabla_archivos, match_PID);

	if (list == NULL)
		return;

	void cerrar_y_liberar(void *elemento) {
		file_descriptor_t fd_global = ((info_pft *)elemento)->fd_global;
		cerrar_fd_global(fd_global);
		free(elemento);
	}

	list_destroy_and_destroy_elements(list->tabla_archivos, cerrar_y_liberar);
	free(list);
}

/* Funciones locales */

static file_descriptor_t add_archivo_global(char *path) {
	file_descriptor_t fd_global = get_global_fd(path);

	info_gft *elemento;

	if (fd_global != -1) {
		elemento = list_get(tabla_archivos_global, fd_global);
		elemento->cantidad++;

		return fd_global;
	}

	else {
		elemento = malloc(sizeof(info_gft));
		elemento->path = strdup(path);
		elemento->cantidad = 1;

		return list_add(tabla_archivos_global, elemento);
	}
}

static file_descriptor_t add_archivo_proceso(int PID, flags_t banderas, file_descriptor_t fd_global) {
	info_pft *elemento = malloc(sizeof(info_pft));
	elemento->banderas = banderas;
	elemento->fd_global = fd_global;

	process_file_table tabla_archivos = get_tabla_archivos_proceso(PID);

	if (tabla_archivos != NULL) {
		tabla_archivos = list_create();

		list_file_table *node = malloc(sizeof(list_file_table));

		node->PID = PID;
		node->tabla_archivos = tabla_archivos;

		list_add(lista_tabla_archivos, node);
	}

	return list_add(tabla_archivos, elemento) + 1;			// FD=0 para stdout
}

static void cerrar_fd_global(file_descriptor_t fd_global) {
	info_gft *info_fd_global = list_get(tabla_archivos_global, fd_global);
	info_fd_global->cantidad--;

	if (info_fd_global->cantidad == 0) {
		free(list_remove(tabla_archivos_global, fd_global));
	}
}

static flags_t get_banderas(int PID, file_descriptor_t fd) {
	info_pft *info_fd = get_fd_info(PID, fd);

	if (info_fd == NULL) {
		flags_t banderas;
		memset(&banderas, 0, sizeof banderas);
		return banderas;
	}

	return info_fd->banderas;
}

static info_pft *get_fd_info(int PID, file_descriptor_t fd) {
	process_file_table tabla_archivos = get_tabla_archivos_proceso(PID);

	if (tabla_archivos == NULL) {
		return NULL;
	}

	return list_get(tabla_archivos, fd - 1);
}

static char *get_fd_path(int PID, file_descriptor_t fd) {
	info_pft *info_fd = get_fd_info(PID, fd);
	if (info_fd == NULL) {
		return NULL;
	}

	info_gft *info_fd_global = list_get(tabla_archivos_global, info_fd->fd_global);

	return info_fd_global->path;
}

static file_descriptor_t get_global_fd(char *path) {
	bool match_path(void *element) {
		char *_path = ((info_gft *)element)->path;
		return strcmp(path, _path) == 0;
	}

	file_descriptor_t index;
	info_gft *p;

	for (index = 0; (p = list_get(tabla_archivos_global, index)) != NULL; index++) {
		if (match_path(p)) {
			return index;
		}
	}

	return -1;
}

static cursor_t get_posicion_cursor(int PID, file_descriptor_t fd) {
	info_pft *info_fd = get_fd_info(PID, fd);
	return (info_fd == NULL) ? -1 : info_fd->posicion;
}

static process_file_table get_tabla_archivos_proceso(int PID) {
	bool match_PID(void *element) {
		int _PID = ((list_file_table *)element)->PID;
		return _PID == PID;
	}
	return list_find(lista_tabla_archivos, match_PID);
}


/*
 * Serializadores
 */

static inline size_t size_header() {
	return sizeof(headerDeLosRipeados);
}

static void *serialize_paquete_validar(char *path) {
	void *stream = malloc(size_header() + strlen(path));

	headerDeLosRipeados header;
	header.codigoDeOperacion = VALIDAR_ARCHIVO;
	header.bytesDePayload = strlen(path);

	memcpy(stream, &header, size_header());
	memcpy(stream + size_header(), path, strlen(path));

	return stream;
}

static void *serialize_paquete_crear(char *path) {
	void *stream = malloc(size_header() + strlen(path));

	headerDeLosRipeados header;
	header.codigoDeOperacion = CREAR_ARCHIVO;
	header.bytesDePayload = strlen(path);

	memcpy(stream, &header, size_header());
	memcpy(stream + size_header(), path, strlen(path));

	return stream;
}

static void *serialize_paquete_leer(char *path, off_t offset, size_t size) {
	void *stream = malloc(size_header() + strlen(path) + sizeof(off_t) + sizeof(size_t));

	headerDeLosRipeados header;
	header.codigoDeOperacion = LEER_ARCHIVO;
	header.bytesDePayload = strlen(path);

	size_t stream_offset = 0;

	memcpy(stream, &header, size_header());						stream_offset += size_header();

	memcpy(stream + stream_offset, path, strlen(path));			stream_offset += strlen(path);
	memcpy(stream + stream_offset, &offset, sizeof(off_t));		stream_offset += sizeof(off_t);
	memcpy(stream + stream_offset, &size, sizeof(size_t));

	return stream;
}

static void *serialize_paquete_escribir(char *path, off_t offset, size_t size,
									size_t buffer_size, void *buffer) {
	void *stream = malloc(size_header() + strlen(path)
							+ sizeof(off_t) + sizeof(size_t)
							+ sizeof(size_t) + buffer_size);

	headerDeLosRipeados header;
	header.codigoDeOperacion = LEER_ARCHIVO;
	header.bytesDePayload = strlen(path);

	size_t stream_offset = 0;

	memcpy(stream, &header, size_header());							stream_offset += size_header();

	memcpy(stream + stream_offset, path, strlen(path));				stream_offset += strlen(path);
	memcpy(stream + stream_offset, &offset, sizeof(off_t));			stream_offset += sizeof(off_t);
	memcpy(stream + stream_offset, &size, sizeof(size_t));				stream_offset += sizeof(size_t);
	memcpy(stream + stream_offset, &buffer_size, sizeof(size_t));		stream_offset += sizeof(size_t);
	memcpy(stream + stream_offset, buffer, buffer_size);

	return stream;
}
