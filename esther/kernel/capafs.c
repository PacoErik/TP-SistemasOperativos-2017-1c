#include "kernel.h"
#include "capafs.h"

static int socket_fs;

global_file_table tabla_archivos_global;

listaProcesos *procesos;

static file_descriptor_t		add_archivo_global			(char *path);
static file_descriptor_t		add_archivo_proceso			(int PID, flags_t banderas, file_descriptor_t fd_global);
static void					cerrar_fd_global				(file_descriptor_t fd_global);
static flags_t					get_banderas					(int PID, file_descriptor_t fd);
static info_pft *				get_fd_info					(int PID, file_descriptor_t fd);
static char *					get_fd_path					(int PID, file_descriptor_t fd);
static file_descriptor_t		get_global_fd					(char *path);
static cursor_t					get_posicion_cursor			(int PID, file_descriptor_t fd);
static process_file_table		get_tabla_archivos_proceso	(int PID);

static void *		serialize_paquete_validar			(char *path, size_t *serialize_size);
static void *		serialize_paquete_borrar			(char *path, size_t *serialize_size);
static void *		serialize_paquete_crear			(char *path, size_t *serialize_size);
static void *		serialize_paquete_leer			(char *path, off_t offset, size_t size, size_t *serialize_size);
static void *		serialize_paquete_escribir		(char *path, off_t offset, size_t size, void *buffer, size_t *serialize_size);

int fs_conectar(void) {
	struct sockaddr_in fs_info;

	fs_info.sin_family = AF_INET;
	fs_info.sin_addr.s_addr = inet_addr( (char*) IP_FS);
	fs_info.sin_port = htons(PUERTO_FS);

	socket_fs = socket(AF_INET, SOCK_STREAM, 0);

	int ret = connect(socket_fs, (struct sockaddr *) &fs_info, sizeof(fs_info));

	if (ret != 0) {
		close(socket_fs);
		return -1;
	}

	handshake(socket_fs, KERNEL);

	return socket_fs;
}

file_descriptor_t fs_abrir_archivo(int PID, char *path, flags_t banderas) {
	/* FS: Validar si existe el archivo */
	size_t paquete_size;
	void *paquete = serialize_paquete_validar(path, &paquete_size);

	send(socket_fs, paquete, paquete_size, 0);
	free(paquete);

	bool existe_archivo;
	recv(socket_fs, &existe_archivo, sizeof existe_archivo, 0);

	if (banderas.creacion) {
		if (existe_archivo) {
			logear_info("Error al crear archivo \"%s\": el archivo ya existe.", path);

			return ARCHIVO_YA_EXISTE;
		}

		logear_info("Creando archivo %s", path);

		paquete = serialize_paquete_crear(path, &paquete_size);
		send(socket_fs, paquete, paquete_size, 0);
		free(paquete);

		bool respuesta;
		recv(socket_fs, &respuesta, sizeof respuesta, 0);

		if (!respuesta) {
			logear_info("No se pudo crear el archivo \"%s\".", path);

			/* Esto suele pasar cuando intenta crear un archivo
			 * cuando ya hay un directorio con el mismo nombre */
			return NO_SE_PUDO_CREAR_ARCHIVO;
		}

		logear_info("Archivo creado: \"%s\"", path);
	}

	else if (!existe_archivo) {
		logear_info("Error al abrir \"%s\": el archivo no existe.", path);
		return ARCHIVO_NO_EXISTE;
	}

	file_descriptor_t fd_global = add_archivo_global(path);
	file_descriptor_t fd = add_archivo_proceso(PID, banderas, fd_global);

	return fd;
}

bool fs_borrar_archivo(int PID, file_descriptor_t fd) {
	info_gft *info_global = list_get(tabla_archivos_global, get_fd_info(PID, fd)->fd_global);
	if (info_global->cantidad > 1) {
		return 0;
	}

	char *path = get_fd_path(PID, fd);

	size_t paquete_size;
	void *paquete = serialize_paquete_borrar(path, &paquete_size);

	send(socket_fs, paquete, paquete_size, 0);
	free(paquete);

	bool respuesta;
	recv(socket_fs, &respuesta, sizeof respuesta, 0);

	return respuesta;
}

void *fs_leer_archivo(int PID, file_descriptor_t fd, size_t tamanio, int *errorcode) {
	if (!get_banderas(PID, fd).lectura) {
		*errorcode = INTENTO_LEER_SIN_PERMISOS;
		return NULL;
	}

	char *path = get_fd_path(PID, fd);
	cursor_t desplazamiento = get_posicion_cursor(PID, fd);

	/* Pedido al File System */
	size_t paquete_size;
	void *paquete = serialize_paquete_leer(path, desplazamiento, tamanio, &paquete_size);

	send(socket_fs, paquete, paquete_size, 0);
	free(paquete);

	bool respuesta;
	recv(socket_fs, &respuesta, sizeof respuesta, 0);

	if (!respuesta) {
		/* Esto ocurre cuando intenta leer mas datos que lo que el archivo contiene */
		*errorcode = ERROR_LECTURA_ARCHIVO;
		return NULL;
	}

	void *data = malloc(tamanio);
	recv(socket_fs, data, tamanio, 0);

	errorcode = SIN_ERROR;		// Re que no se usa para eso
	return data;
}

bool fs_cerrar_archivo(int PID, file_descriptor_t fd) {
	process_file_table tabla_archivos = get_tabla_archivos_proceso(PID);
	info_pft *info_fd = list_remove(tabla_archivos, fd - 1);

	if (info_fd == NULL) {
		/* Esta queriendo cerrar un archivo que no esta abierto?! */
		return 0;
	}

	cerrar_fd_global(info_fd->fd_global);
	free(info_fd);

	return 1;
}

int fs_escribir_archivo(int PID, file_descriptor_t fd, void *datos, size_t tamanio) {
	if (!get_banderas(PID, fd).escritura) {
		return INTENTO_ESCRIBIR_SIN_PERMISOS;
	}

	char *path = get_fd_path(PID, fd);
	cursor_t desplazamiento = get_posicion_cursor(PID, fd);

	/* Pedido al File System */
	size_t paquete_size;
	void *paquete = serialize_paquete_escribir(path, desplazamiento, tamanio, datos, &paquete_size);

	send(socket_fs, paquete, paquete_size, 0);
	free(paquete);

	bool respuesta;
	recv(socket_fs, &respuesta, sizeof respuesta, 0);

	return respuesta;
}

void init_tabla_archivos(void) {
	tabla_archivos_global = list_create();
}

void destroy_tabla_archivos_proceso(process_file_table tabla) {
	if (tabla == NULL)
		return;

	void cerrar_y_liberar(void *elemento) {
		file_descriptor_t fd_global = ((info_pft *)elemento)->fd_global;
		cerrar_fd_global(fd_global);
		free(elemento);
	}

	list_destroy_and_destroy_elements(tabla, cerrar_y_liberar);
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
	elemento->posicion = 0;

	process_file_table tabla_archivos = get_tabla_archivos_proceso(PID);

	/* TODO: Buscar un elemento null de la tabla y reemplazarlo */

	return list_add(tabla_archivos, elemento) + DESCRIPTOR_SALIDA + 1;
}

static void cerrar_fd_global(file_descriptor_t fd_global) {
	info_gft *info_fd_global = list_get(tabla_archivos_global, fd_global);
	info_fd_global->cantidad--;

	if (info_fd_global->cantidad == 0) {
		logear_info("[Archivo global] Se cierra %s", info_fd_global->path);
		free(info_fd_global->path);
		free(info_fd_global);
		info_fd_global = NULL;
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

	return list_get(tabla_archivos, fd - DESCRIPTOR_SALIDA - 1);
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
		if (element == NULL) {
			return 0;
		}

		char *_path = ((info_gft *)element)->path;
		return strcmp(path, _path) == 0;
	}

	info_gft *p;
	file_descriptor_t i;

	for (i = 0; i < list_size(tabla_archivos_global); i++) {
		p = list_get(tabla_archivos_global, i);
		if (p != NULL) {
			if (match_path(p)) return i;
		}
	}

	return -1;
}

static cursor_t get_posicion_cursor(int PID, file_descriptor_t fd) {
	info_pft *info_fd = get_fd_info(PID, fd);
	return (info_fd == NULL) ? -1 : info_fd->posicion;
}

static process_file_table get_tabla_archivos_proceso(int PID) {
	bool match_PID(void* elemento) {
		return PID == ((Proceso*) elemento)->pcb->pid;
	}
	Proceso *proceso = list_find(procesos, match_PID);

	return (proceso == NULL) ? NULL : proceso->pcb->tabla_archivos;
}


/*
 * Serializadores
 */

static inline size_t size_header() {
	return sizeof(headerDeLosRipeados);
}

static void *serialize_paquete_validar(char *path, size_t *serialize_size) {
	void *stream = malloc(size_header() + strlen(path));

	headerDeLosRipeados header;
	header.codigoDeOperacion = VALIDAR_ARCHIVO;
	header.bytesDePayload = strlen(path);

	memcpy(stream, &header, size_header());
	memcpy(stream + size_header(), path, strlen(path));

	*serialize_size = size_header() + strlen(path);

	return stream;
}

static void *serialize_paquete_borrar(char *path, size_t *serialize_size) {
	void *stream = malloc(size_header() + strlen(path));

	headerDeLosRipeados header;
	header.codigoDeOperacion = BORRAR_ARCHIVO;
	header.bytesDePayload = strlen(path);

	memcpy(stream, &header, size_header());
	memcpy(stream + size_header(), path, strlen(path));

	*serialize_size = size_header() + strlen(path);

	return stream;
}

static void *serialize_paquete_crear(char *path, size_t *serialize_size) {
	void *stream = malloc(size_header() + strlen(path));

	headerDeLosRipeados header;
	header.codigoDeOperacion = CREAR_ARCHIVO;
	header.bytesDePayload = strlen(path);

	memcpy(stream, &header, size_header());
	memcpy(stream + size_header(), path, strlen(path));

	*serialize_size = size_header() + strlen(path);

	return stream;
}

static void *serialize_paquete_leer(char *path, off_t offset, size_t size, size_t *serialize_size) {
	void *stream = malloc(size_header() + strlen(path) + sizeof(off_t) + sizeof(size_t));

	headerDeLosRipeados header;
	header.codigoDeOperacion = LEER_ARCHIVO;
	header.bytesDePayload = strlen(path);

	size_t stream_offset = 0;

	memcpy(stream, &header, size_header());						stream_offset += size_header();

	memcpy(stream + stream_offset, path, strlen(path));			stream_offset += strlen(path);
	memcpy(stream + stream_offset, &offset, sizeof(off_t));		stream_offset += sizeof(off_t);
	memcpy(stream + stream_offset, &size, sizeof(size_t));

	*serialize_size = stream_offset + sizeof(size_t);

	return stream;
}

static void *serialize_paquete_escribir(char *path, off_t offset, size_t size,
											void *buffer, size_t *serialize_size) {
	void *stream = malloc(size_header() + strlen(path)
							+ sizeof(off_t) + sizeof(size_t)
							+ sizeof(size_t) + size);

	headerDeLosRipeados header;
	header.codigoDeOperacion = ESCRIBIR_ARCHIVO;
	header.bytesDePayload = strlen(path);

	size_t stream_offset = 0;

	memcpy(stream, &header, size_header());							stream_offset += size_header();

	memcpy(stream + stream_offset, path, strlen(path));				stream_offset += strlen(path);
	memcpy(stream + stream_offset, &offset, sizeof(off_t));			stream_offset += sizeof(off_t);
	memcpy(stream + stream_offset, &size, sizeof(size_t));				stream_offset += sizeof(size_t);
	memcpy(stream + stream_offset, buffer, size);

	*serialize_size = stream_offset + size;

	return stream;
}
