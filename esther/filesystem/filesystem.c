#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "commons/bitarray.h"

#include "qepd/qepd.h"

#define ROUNDUP(x,y) ((x - 1) / y + 1)			// Redondear hacia arriba
#define CANTIDAD_BLOQUES_ARCHIVO(FILE_SIZE, BLOCK_SIZE) ((FILE_SIZE > 0) ? ROUNDUP(FILE_SIZE, BLOCK_SIZE) : 1)

int servidor;	// Socket Kernel

t_log* 			logger;
t_config* 		config;
t_bitarray*		bitmap;

struct {
	char IP_KERNEL[sizeof "255.255.255.255"];
	char IP_MEMORIA[sizeof "255.255.255.255"];
	int PUERTO_KERNEL;
	int PUERTO_MEMORIA;
	char PUNTO_MONTAJE[64];
} FSConfig;

struct {
	unsigned int TAMANIO_BLOQUES;
	unsigned int CANTIDAD_BLOQUES;
	char MAGIC_NUMBER[sizeof "SADICA"];
} FSMetadata;

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

comando comandos[] = {
		{ "validar",	FS_VALIDAR	},
		{ "crear",		FS_CREAR	},
		{ "eliminar",	FS_ELIMINAR	},
		{ "leer",		FS_LEER		},
		{ "escribir",	FS_ESCRIBIR	}
};

/* Operaciones del File System */

bool		validar_archivo				(char*);
bool		crear_archivo				(char*);
bool		eliminar_archivo			(char*);
char*		leer_archivo				(char*, off_t, size_t);
bool		escribir_archivo			(char*, off_t, size_t, char*);


/* Operaciones del Bitmap */

bool			actualizar_archivo_bitmap		(void);
t_bitarray*		crear_bitmap_vacio			(void);
t_bitarray*		leer_bitmap					(void);
t_bitarray*		limpiar_bitmap					(void);
void			destruir_bitmap				(void);


/* Otros */

void		leer_mensaje					(void);
void		leer_metadata					(void);
void		establecer_configuracion		(void);
void		interaccion_FS					(void);


/* Funciones auxiliares */

int*			_asignar_bloques					(int, int**);
bool			_actualizar_metadata_bitmap		(char*, FileMetadata*);
char*			_ruta_desde_punto_montaje			(char*);
char*			_ruta_desde_archivos				(char*);
FileMetadata*	_leer_metadata_archivo			(char*);
void			_destruir_metadata_archivo		(FileMetadata*);
void			_liberar_bloques					(FileMetadata*);
FILE*			_archivo_bloque_r					(int);
FILE*			_archivo_bloque_w					(int);
bool			_crear_directorios				(char*);

int main(void) {
	configurar("filesystem");
	leer_metadata();

	bitmap = leer_bitmap();
	//limpiar_bitmap();

	interaccion_FS();

	destruir_bitmap();

	return 0;
}

/*
 * Esta hecho asi no mas para testear
 */
void interaccion_FS(void) {
	char input_comando[10];
	char input_ruta[100];
	int input_offset;
	int input_size;
	char input_buffer[1000];

	printf("--------------------------\n"
			"Comandos:\n"
			"validar [RUTA_ARCHIVO]\n"
			"crear [RUTA_ARCHIVO]\n"
			"eliminar [RUTA_ARCHIVO]\n"
			"leer [RUTA_ARCHIVO] [DESPLAZAMIENTO] [TAMANIO]\n"
			"escribir [RUTA_ARCHIVO] [DESPLAZAMIENTO] [TAMANIO] [CONTENIDO]\n"
			"salir\n");
	fflush(stdout);

	while (1) {
		scanf("%s", input_comando);

		if (strcmp(input_comando, "salir") == 0) {
			break;
		}

		int i;
		for (i = 0; i < (sizeof comandos / sizeof *comandos); i++) {
			if (strcmp(comandos[i].nombre, input_comando) == 0) {
				scanf("%s", input_ruta);
				switch (comandos[i].op) {
				case FS_VALIDAR:
					printf("%s\n", validar_archivo(input_ruta) ? "True" : "False");
					break;

				case FS_CREAR:
					printf("%s\n", crear_archivo(input_ruta) ? "Archivo creado" : "Error");
					break;

				case FS_ELIMINAR:
					printf("%s\n", eliminar_archivo(input_ruta) ? "Archivo eliminado" : "Error");
					break;

				case FS_LEER:
					scanf("%d", &input_offset);
					scanf("%d", &input_size);
					char *contenido = leer_archivo(input_ruta, input_offset, input_size);
					printf("Contenido archivo: \"%.*s\"\n", input_size, contenido);
					free(contenido);

					break;

				case FS_ESCRIBIR:
					scanf("%d", &input_offset);
					scanf("%d", &input_size);
					scanf("%s", input_buffer);
					printf("%s\n",
							escribir_archivo(input_ruta, input_offset,
									input_size, input_buffer) ?
									"Archivo actualizado" : "Error");
					break;
				}

				goto continue_outer; // for
			}
		}

		printf("\"%s\" no es un comando!!\n", input_comando);

		continue_outer:
		;
	}
}

void leer_mensaje(void) {
	int bytesRecibidos;
	char mensaje[512];
	bytesRecibidos = recv(servidor, &mensaje, sizeof(mensaje), 0);
	mensaje[bytesRecibidos] = '\0';
	if (bytesRecibidos <= 0) {
		close(servidor);
		logear_error("Servidor desconectado luego de intentar leer mensaje", true);
	}
	logear_info("Mensaje recibido: %s", mensaje);
}

void leer_metadata(void) {
	char *ruta_metadata = _ruta_desde_punto_montaje("Metadata/Metadata.bin");

	t_config *metadata_config;
	metadata_config = config_create(ruta_metadata);

	if (metadata_config == NULL) {
		free(ruta_metadata);
		config_destroy(metadata_config);
		logear_error("No existe el archivo \"Metadata.bin\"", true);
	}

	free(ruta_metadata);
	logear_info("Leyendo \"Metadata.bin\"...");

	if (config_has_property(metadata_config, "TAMANIO_BLOQUES")) {
		FSMetadata.TAMANIO_BLOQUES = config_get_int_value(metadata_config, "TAMANIO_BLOQUES");
		logear_info("Tamaño de Bloques: %d", FSMetadata.TAMANIO_BLOQUES);
	}
	else {
		logear_error("Error al leer el tamaño de bloques", false);
		goto error;
	}

	if (config_has_property(metadata_config, "CANTIDAD_BLOQUES")) {
		FSMetadata.CANTIDAD_BLOQUES = config_get_int_value(metadata_config, "CANTIDAD_BLOQUES");
		logear_info("Cantidad de Bloques: %d", FSMetadata.CANTIDAD_BLOQUES);
	}
	else {
		logear_error("Error al leer la cantidad de bloques", false);
		goto error;
	}

	if (config_has_property(metadata_config, "MAGIC_NUMBER")) {
		strncpy(FSMetadata.MAGIC_NUMBER,
				config_get_string_value(metadata_config, "MAGIC_NUMBER"),
				sizeof FSMetadata.MAGIC_NUMBER);
		logear_info("Magic Number: %s", FSMetadata.MAGIC_NUMBER);
	}
	else {
		logear_error("Error al leer el Magic Number!!", false);
		goto error;
	}

	config_destroy(metadata_config);
	return;

error:
	config_destroy(metadata_config);
	exit(EXIT_FAILURE);
}

bool validar_archivo(char *ruta) {
	char *ruta_completa = _ruta_desde_archivos(ruta);

	FILE *archivo = fopen(ruta_completa, "r");

	free(ruta_completa);

	if (archivo != NULL) {
		fclose(archivo);
		return 1;
	}

	return 0;
}

/*
 * Crea un archivo vacio dentro de la ruta solicitada.
 */
bool crear_archivo(char *ruta) {
	if (_crear_directorios(ruta) == 0) {
		logear_error("No se pudo crear el directorio para el archivo.", false);
		return 0;
	}

	int *bloques = malloc(sizeof(int));

	_asignar_bloques(1, &bloques);
	if (bloques == NULL) {
		logear_error("No se pudo crear el archivo: Espacio insuficiente", false);
		return 0;
	}

	FileMetadata *file_md = malloc(sizeof(FileMetadata));
	file_md->tamanio = 0;
	file_md->bloques = bloques;

	bool result = _actualizar_metadata_bitmap(ruta, file_md);
	
	_destruir_metadata_archivo(file_md);

	return result;
}

/*
 * Crea los directorios que contiene al archivo.
 */
bool _crear_directorios(char *ruta) {
	char *dir = calloc(strlen(ruta) + 1, sizeof(char));

	int i;
	for (i = 0; i != strlen(ruta); i++) {
		if (ruta[i] != '/') {
			continue;
		}

		strncpy(dir, ruta, i);

		if (access(_ruta_desde_archivos(dir), F_OK) == 0) {
			continue;
		}

		if (mkdir(_ruta_desde_archivos(dir), 777) == -1) {
			if (errno == ENOTDIR) {
				logear_error("\"%s\" no es un directorio.\n", false, dir);

				free(dir);
				return 0;
			}
		}
	}

	free(dir);
	return 1;
}

bool _actualizar_metadata_bitmap(char *ruta, FileMetadata *file_md) {
	char *ruta_completa = _ruta_desde_archivos(ruta);

	FILE *archivo = fopen(ruta_completa, "w");
	free(ruta_completa);

	if (archivo == NULL) {
		if (errno == ENOTDIR) {
			logear_error("No se pudo crear el directorio para el archivo.", false);
		}
		return 0;
	}

	fprintf(archivo, "TAMANIO=%d\n", file_md->tamanio);
	fprintf(archivo, "BLOQUES=[");

	int cantidad_bloques = CANTIDAD_BLOQUES_ARCHIVO(file_md->tamanio, FSMetadata.TAMANIO_BLOQUES);

	int i;
	for (i = 0; ; i++) {
		bitarray_set_bit(bitmap, file_md->bloques[i] - 1); // Numero de bloques empieza desde 1
		fprintf(archivo, "%d", file_md->bloques[i]);

		if (i == cantidad_bloques - 1) {
			fprintf(archivo, "]");
			break;
		}

		fprintf(archivo, ",");
	}

	fclose(archivo);

	actualizar_archivo_bitmap();

	return 1;
}

char *_ruta_desde_punto_montaje(char *ruta) {
	char *ruta_completa;
	asprintf(&ruta_completa, ".%s%s", FSConfig.PUNTO_MONTAJE, ruta);

	return ruta_completa;
}

char *_ruta_desde_archivos(char *ruta) {
	char *ruta_archivo;
	asprintf(&ruta_archivo, "Archivos/%s", ruta);

	char *ruta_completa = _ruta_desde_punto_montaje(ruta_archivo);

	free(ruta_archivo);

	return ruta_completa;
}

FileMetadata *_leer_metadata_archivo(char *ruta) {
	char *ruta_completa = _ruta_desde_archivos(ruta);

	t_config *metadata_config = config_create(ruta_completa);

	free(ruta_completa);

	if (metadata_config == NULL) {
		return NULL;
	}

	if (config_has_property(metadata_config, "TAMANIO") == 0) {
		goto cleanup;
	}

	if (config_has_property(metadata_config, "BLOQUES") == 0) {
		goto cleanup;
	}

	char **tmp = config_get_array_value(metadata_config, "BLOQUES");
	if (tmp == NULL) {
		goto cleanup;
	}

	FileMetadata *file_md = malloc(sizeof(FileMetadata));
	file_md->tamanio = config_get_int_value(metadata_config, "TAMANIO");
	file_md->bloques = NULL;

	int i;
	for (i = 0; tmp[i] != NULL; i++) {
		file_md->bloques = realloc(file_md->bloques, (i + 1) * sizeof(int));
		file_md->bloques[i] = atoi(tmp[i]);
		free(tmp[i]);
	}

	free(tmp);
	config_destroy(metadata_config);

	return file_md;

cleanup:
	config_destroy(metadata_config);
	return NULL;
}

bool eliminar_archivo(char *ruta) {
	if (validar_archivo(ruta) == 0) {
		return 0;
	}

	FileMetadata *file_md = _leer_metadata_archivo(ruta);

	_liberar_bloques(file_md);

	char *ruta_completa = _ruta_desde_archivos(ruta);
	unlink(ruta_completa);

	free(ruta_completa);
	_destruir_metadata_archivo(file_md);

	return 1;
}

void _destruir_metadata_archivo(FileMetadata *file_md) {
	free(file_md->bloques);
	free(file_md);
}

void _liberar_bloques(FileMetadata *file_md) {
	int cantidad_bloques = CANTIDAD_BLOQUES_ARCHIVO(file_md->tamanio, FSMetadata.TAMANIO_BLOQUES);

	int i;
	for (i = 0; i < cantidad_bloques; i++) {
		bitarray_clean_bit(bitmap, file_md->bloques[i] - 1);
	}
}

char *leer_archivo(char *ruta, off_t offset, size_t size) {
	FileMetadata *file_md = _leer_metadata_archivo(ruta);

	if (file_md == NULL) {
		return NULL;
	}

	if (offset < 0 || size <= 0) {
		goto cleanup_1;
	}

	if (offset + size > file_md->tamanio) {
		goto cleanup_1;
	}

	FILE *archivo_bloque;

	int i_bloque;			// Indice de bloque
	off_t offset_bloque;	// Desplazamiento dentro del bloque

	// Abre el .bin del primer bloque a leer y setea el desplazamiento
	for (i_bloque = 0, offset_bloque = offset; ;
			i_bloque++, offset_bloque -= FSMetadata.TAMANIO_BLOQUES) {
		if (offset_bloque < FSMetadata.TAMANIO_BLOQUES) {
			archivo_bloque = _archivo_bloque_r(file_md->bloques[i_bloque]);
			fseek(archivo_bloque, offset_bloque, SEEK_SET);

			break;
		}
	}

	size_t bytes;				// Bytes leidos en cada iteracion
	size_t bytes_a_leer;		// Cantidad de bytes a leer en la siguiente iteracion
	size_t bytes_leidos;		// Cantidad total de bytes leidos
	char *data;					// Almacena todos los bytes leidos

	bytes_a_leer = ((FSMetadata.TAMANIO_BLOQUES - offset_bloque) > size)
						? size
						: FSMetadata.TAMANIO_BLOQUES - offset_bloque;
	bytes_leidos = 0;
	data = calloc(size + 1, sizeof(char));

	while (1) {
		bytes = fread(&data[bytes_leidos], sizeof(char), bytes_a_leer, archivo_bloque);

		if (archivo_bloque == NULL) {
			goto cleanup_2;
		}

		fclose(archivo_bloque);

		if (bytes != bytes_a_leer) {
			goto cleanup_2;
		}

		bytes_leidos += bytes;

		if (bytes_leidos == size) {
			_destruir_metadata_archivo(file_md);

			return data;
		}

		i_bloque++;

		archivo_bloque = _archivo_bloque_r(file_md->bloques[i_bloque]);

		bytes_a_leer = ((size - bytes_leidos) > FSMetadata.TAMANIO_BLOQUES)
						? FSMetadata.TAMANIO_BLOQUES
						: (size - bytes_leidos);
	}

cleanup_2:
	free(data);
cleanup_1:
	_destruir_metadata_archivo(file_md);

	return NULL;
}

FILE *_archivo_bloque_r(int bloque_numero) {
	char *ruta_bloque;
	asprintf(&ruta_bloque, ".%sBloques/%d.bin", FSConfig.PUNTO_MONTAJE, bloque_numero);

	FILE *archivo_bloque = fopen(ruta_bloque, "r");

	free(ruta_bloque);

	return archivo_bloque;
}

FILE *_archivo_bloque_w(int bloque_numero) {
	char *ruta_bloque;
	asprintf(&ruta_bloque, ".%sBloques/%d.bin", FSConfig.PUNTO_MONTAJE, bloque_numero);

	FILE *archivo_bloque = fopen(ruta_bloque, "r+");
	if (archivo_bloque == NULL) {
		archivo_bloque = fopen(ruta_bloque, "w");
	}

	free(ruta_bloque);

	return archivo_bloque;
}

bool escribir_archivo(char *ruta, off_t offset, size_t size, char *buffer) {
	FileMetadata *file_md = _leer_metadata_archivo(ruta);

	if (file_md == NULL) {
		return 0;
	}

	if (offset < 0 || size <= 0) {
		_destruir_metadata_archivo(file_md);
		return 0;
	}

	/* Debe actualizar el tamanio */
	if (offset + size > file_md->tamanio) {
		// Cantidad de bloques actual
		int old_block_n = CANTIDAD_BLOQUES_ARCHIVO(file_md->tamanio, FSMetadata.TAMANIO_BLOQUES);

		// Cantidad de bloques que se necesita para contener los datos
		int block_n_needed = CANTIDAD_BLOQUES_ARCHIVO(offset + size, FSMetadata.TAMANIO_BLOQUES);

		/* Asignar mas bloques */
		if (block_n_needed > old_block_n) {

			file_md->bloques = realloc(file_md->bloques, block_n_needed * sizeof(int));

			int *bloques_nuevos = file_md->bloques + old_block_n;
			_asignar_bloques(block_n_needed - old_block_n, &bloques_nuevos);

			if (bloques_nuevos == NULL) {
				/* Espacio Insuficiente */
				_destruir_metadata_archivo(file_md);
				return 0;
			}
		}

		file_md->tamanio = offset + size;
		_actualizar_metadata_bitmap(ruta, file_md);
	}

	FILE *archivo_bloque;

	int i_bloque;			// Indice de bloque
	off_t offset_bloque;	// Desplazamiento dentro del bloque

	// Abre el .bin del primer bloque a escribir y setea el desplazamiento
	for (i_bloque = 0, offset_bloque = offset; ;
			i_bloque++, offset_bloque -= FSMetadata.TAMANIO_BLOQUES) {
		if (offset_bloque < FSMetadata.TAMANIO_BLOQUES) {
			archivo_bloque = _archivo_bloque_w(file_md->bloques[i_bloque]);
			fseek(archivo_bloque, offset_bloque, SEEK_SET);

			break;
		}
	}

	size_t bytes;				// Bytes escritos en cada iteracion
	size_t bytes_a_escribir;	// Cantidad de bytes a escribir en la siguiente iteracion
	size_t bytes_escritos;		// Cantidad total de bytes escritos

	bytes_a_escribir = ((FSMetadata.TAMANIO_BLOQUES - offset_bloque) > size)
						? size
						: FSMetadata.TAMANIO_BLOQUES - offset_bloque;
	bytes_escritos = 0;

	while (1) {
		bytes = fwrite(&buffer[bytes_escritos], sizeof(char), bytes_a_escribir, archivo_bloque);
		fflush(archivo_bloque);

		if (archivo_bloque == NULL) {
			_destruir_metadata_archivo(file_md);
			return 0;
		}

		fclose(archivo_bloque);

		if (bytes != bytes_a_escribir) {
			_destruir_metadata_archivo(file_md);
			return 0;
		}

		bytes_escritos += bytes;

		if (bytes_escritos == size) {
			_destruir_metadata_archivo(file_md);
			return 1;
		}

		i_bloque++;

		archivo_bloque = _archivo_bloque_w(file_md->bloques[i_bloque]);

		bytes_a_escribir = ((size - bytes_escritos) > FSMetadata.TAMANIO_BLOQUES)
						? FSMetadata.TAMANIO_BLOQUES
						: (size - bytes_escritos);
	}
}

t_bitarray *leer_bitmap(void) {
	char *ruta_bitmap = _ruta_desde_punto_montaje("Metadata/Bitmap.bin");

	FILE *bitmap_file = fopen(ruta_bitmap, "r");
	free(ruta_bitmap);

	if (bitmap_file == NULL) {
		logear_error("No existe el archivo \"Bitmap.bin\"", false);
		return NULL;
	}

	size_t bitarray_size = ROUNDUP(FSMetadata.CANTIDAD_BLOQUES, CHAR_BIT); // CHAR_BIT = cantidad bits x char

	char *bitarray = malloc(bitarray_size);

	size_t read_bytes = fread(bitarray, 1, bitarray_size, bitmap_file);
	if (read_bytes != bitarray_size) {
		fclose(bitmap_file);
		free(bitarray);
		logear_error("El Bitmap esta incompleto", false);
		return NULL;
	}

	fclose(bitmap_file);

	return bitarray_create_with_mode(bitarray, bitarray_size, LSB_FIRST);
}

t_bitarray *limpiar_bitmap(void) {
	memset(bitmap->bitarray, 0, bitmap->size);
	actualizar_archivo_bitmap();
	return bitmap;
}

t_bitarray *crear_bitmap_vacio(void) {
	size_t bytes = ROUNDUP(FSMetadata.CANTIDAD_BLOQUES, CHAR_BIT);
	char *bitarray = calloc(bytes, sizeof(char));
	return bitarray_create_with_mode(bitarray, bytes, LSB_FIRST);
}

void destruir_bitmap(void) {
	free(bitmap->bitarray);
	bitarray_destroy(bitmap);
}

bool actualizar_archivo_bitmap(void) {
	char *ruta = _ruta_desde_punto_montaje("Metadata/Bitmap.bin");

	FILE *bitmap_file = fopen(ruta, "w");
	free(ruta);

	if (bitmap_file == NULL) {
		logear_error("No se pudo actualizar el archivo \"Bitmap.bin\"", false);
		return 0;
	}

	int bytes = fwrite(bitmap->bitarray, sizeof(char), bitmap->size, bitmap_file);
	if (bytes != bitmap->size) {
		logear_error("Error al actualizar el archivo \"Bitmap.bin\"", false);
		fclose(bitmap_file);
		return 0;
	}

	fclose(bitmap_file);

	return 1;
}

/**
 * Devuelve n bloques libres.
 */
int *_asignar_bloques(int n, int **bloques) {
	int *tmp = malloc(sizeof(int[n]));

	int i;
	int found_n; // Cantidad de bloques encontrados

	for (i = 0, found_n = 0; i < bitarray_get_max_bit(bitmap) && found_n < n; i++) {
		/* Encuentra bloques libres */
		if (bitarray_test_bit(bitmap, i) == 0) {
			tmp[found_n] = i + 1;
			found_n++;
		}
	}

	if (found_n != n) {
		free(tmp);
		bloques = NULL;
		return NULL;
	}

	memcpy(*bloques, tmp, n * sizeof(int));

	free(tmp);

	return *bloques;
}

void establecer_configuracion(void) {
	if (config_has_property(config, "PUERTO_KERNEL")) {
		FSConfig.PUERTO_KERNEL = config_get_int_value(config, "PUERTO_KERNEL");
		logear_info("Puerto Kernel: %d", FSConfig.PUERTO_KERNEL);
	}
	else {
		logear_error("Error al leer el puerto del Kernel", true);
	}

	if (config_has_property(config, "IP_KERNEL")) {
		strcpy(FSConfig.IP_KERNEL, config_get_string_value(config, "IP_KERNEL"));
		logear_info("IP Kernel: %s", FSConfig.IP_KERNEL);
	}
	else {
		logear_error("Error al leer la IP del Kernel", true);
	}

	if (config_has_property(config, "PUERTO_MEMORIA")) {
		FSConfig.PUERTO_MEMORIA = config_get_int_value(config, "PUERTO_MEMORIA");
		logear_info("Puerto Memoria: %d", FSConfig.PUERTO_MEMORIA);
	}
	else {
		logear_error("Error al leer el puerto de la Memoria", true);
	}

	if (config_has_property(config, "PUNTO_MONTAJE")) {
		strcpy(FSConfig.PUNTO_MONTAJE, config_get_string_value(config, "PUNTO_MONTAJE"));
		logear_info("Punto de Montaje: %s", FSConfig.PUNTO_MONTAJE);
	}
	else {
		logear_error("Error al leer el punto de montaje del FS", true);
	}
}
