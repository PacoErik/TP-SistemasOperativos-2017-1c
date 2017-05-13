#include <stdio.h>
#include <string.h>

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

/* Operaciones del File System */

bool		existe_archivo				(char*);
bool		crear_archivo				(char*);
bool		eliminar_archivo			(char*);
char*		leer_archivo				(char*, off_t, size_t);
void		escribir_archivo			(char*, off_t, size_t, char*);


/* Operaciones del Bitmap */

bool			actualizar_archivo_bitmap		(void);
t_bitarray*		crear_bitmap_vacio			(void);
t_bitarray*		leer_bitmap					(void);
t_bitarray*		limpiar_bitmap					(void);
void			destruir_bitmap				(void);


/* Otros */

void		leer_mensaje					(void);
void		leer_metadata					(void);
void		establecerConfiguracion		(void);


/* Funciones auxiliares */

int*			_asignar_bloques					(int, int*);
bool			_actualizar_metadata_bitmap		(char*, FileMetadata*);
char*			_ruta_desde_punto_montaje			(char*);
char*			_ruta_desde_archivos				(char*);
FileMetadata*	_leer_metadata_archivo			(char*);
void			_destruir_metadata_archivo		(FileMetadata*);
void			_liberar_bloques					(FileMetadata*);

int main(void) {

	configurar("filesystem");
	leer_metadata();
	//conectar(&servidor, FSConfig.IP_KERNEL, FSConfig.PUERTO_KERNEL);
	//handshake(servidor, FILESYSTEM);

	bitmap = leer_bitmap();

	_leer_metadata_archivo("unarchivo.txt");

	crear_archivo("otroarchivo.txt");

	destruir_bitmap();

	return 0;
}

void leer_mensaje(void) {
	int bytesRecibidos;
	char mensaje[512];
	bytesRecibidos = recv(servidor, &mensaje, sizeof(mensaje), 0);
	mensaje[bytesRecibidos] = '\0';
	if (bytesRecibidos <= 0) {
		close(servidor);
		logearError("Servidor desconectado luego de intentar leer mensaje", true);
	}
	logearInfo("Mensaje recibido: %s", mensaje);
}

void leer_metadata(void) {
	char *ruta_metadata = _ruta_desde_punto_montaje("Metadata/Metadata.bin");

	t_config *metadata_config;
	metadata_config = config_create(ruta_metadata);

	if (metadata_config == NULL) {
		free(ruta_metadata);
		config_destroy(metadata_config);
		logearError("No existe el archivo \"Metadata.bin\"", true);
	}

	free(ruta_metadata);
	logearInfo("Leyendo \"Metadata.bin\"...");

	if (config_has_property(metadata_config, "TAMANIO_BLOQUES")) {
		FSMetadata.TAMANIO_BLOQUES = config_get_int_value(metadata_config, "TAMANIO_BLOQUES");
		logearInfo("Tamaño de Bloques: %d", FSMetadata.TAMANIO_BLOQUES);
	}
	else {
		logearError("Error al leer el tamaño de bloques", false);
		goto error;
	}

	if (config_has_property(metadata_config, "CANTIDAD_BLOQUES")) {
		FSMetadata.CANTIDAD_BLOQUES = config_get_int_value(metadata_config, "CANTIDAD_BLOQUES");
		logearInfo("Cantidad de Bloques: %d", FSMetadata.CANTIDAD_BLOQUES);
	}
	else {
		logearError("Error al leer la cantidad de bloques", false);
		goto error;
	}

	if (config_has_property(metadata_config, "MAGIC_NUMBER")) {
		strncpy(FSMetadata.MAGIC_NUMBER,
				config_get_string_value(metadata_config, "MAGIC_NUMBER"),
				sizeof FSMetadata.MAGIC_NUMBER);
		logearInfo("Magic Number: %s", FSMetadata.MAGIC_NUMBER);
	}
	else {
		logearError("Error al leer el Magic Number!!", false);
		goto error;
	}

	config_destroy(metadata_config);
	return;

error:
	config_destroy(metadata_config);
	exit(EXIT_FAILURE);
}

bool existe_archivo(char *ruta) {
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
	int *bloques;

	_asignar_bloques(1, bloques);
	if (bloques == NULL) {
		logearError("No se pudo crear el archivo: Espacio insuficiente", false);
		return 0;
	}

	FileMetadata *file_md = malloc(sizeof(FileMetadata));
	file_md->tamanio = 0;
	file_md->bloques = bloques;

	bool result = _actualizar_metadata_bitmap(ruta, file_md);
	
	return result;
}

bool _actualizar_metadata_bitmap(char *ruta, FileMetadata *file_md) {
	char *ruta_completa = _ruta_desde_archivos(ruta);

	FILE *archivo = fopen(ruta_completa, "w");
	free(ruta_completa);

	if (archivo == NULL) {
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
		file_md->bloques = realloc(file_md->bloques, i + 1);
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
	if (existe_archivo(ruta) == 0) {
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

	if (offset < 0 || size < 0) {
		return NULL;
	}

	FILE *archivo_bloque;

	size_t bytes_read;
	char *data = NULL;

	int num_bloque;
	off_t offset_bloque; // Desplazamiento dentro del bloque

	for (num_bloque = 0, offset_bloque = offset; ; num_bloque++, offset_bloque -= FSMetadata.TAMANIO_BLOQUES) {
		if (offset_bloque < FSMetadata.TAMANIO_BLOQUES) {
			char *ruta_bloque;
			asprintf(ruta_bloque, ".%sBloques/%d.bin", FSConfig.PUNTO_MONTAJE, file_md->bloques[num_bloque]);
			archivo_bloque = fopen(ruta_bloque, "r");

			free(ruta_bloque);

			fseek(archivo_bloque, offset_bloque, SEEK_SET);
		}
	}

	/* TODO */
}

void escribir_archivo(char *ruta, off_t offset, size_t size, char *buffer) {
	// int fseek ( FILE * stream, long int offset, int origin );
	// size_t fwrite ( const void * ptr, size_t size, size_t count, FILE * stream );
	// char*   string_substring(char* text, int start, int length)

}

t_bitarray *leer_bitmap(void) {
	char *ruta_bitmap = _ruta_desde_punto_montaje("Metadata/Bitmap.bin");

	FILE *bitmap_file = fopen(ruta_bitmap, "r");
	free(ruta_bitmap);

	if (bitmap_file == NULL) {
		logearError("No existe el archivo \"Bitmap.bin\"", false);
		return NULL;
	}

	size_t bitarray_size = ROUNDUP(FSMetadata.CANTIDAD_BLOQUES, CHAR_BIT); // CHAR_BIT = cantidad bits x char

	char *bitarray = malloc(bitarray_size);

	size_t read_bytes = fread(bitarray, 1, bitarray_size, bitmap_file);
	if (read_bytes != bitarray_size) {
		fclose(bitmap_file);
		free(bitarray);
		logearError("El Bitmap esta incompleto", false);
		return NULL;
	}

	fclose(bitmap_file);

	return bitarray_create_with_mode(bitarray, bitarray_size, LSB_FIRST);
}

t_bitarray *limpiar_bitmap(void) {
	memset(bitmap->bitarray, 0, bitmap->size);
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
		logearError("No se pudo actualizar el archivo \"Bitmap.bin\"", false);
		return 0;
	}

	int bytes = fwrite(bitmap->bitarray, sizeof(char), bitmap->size, bitmap_file);
	if (bytes != bitmap->size) {
		logearError("Error al actualizar el archivo \"Bitmap.bin\"", false);
		fclose(bitmap_file);
		return 0;
	}

	fclose(bitmap_file);

	return 1;
}

/**
 * Devuelve n bloques libres.
 */
int *_asignar_bloques(int n, int *bloques) {
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

	bloques = tmp;
	for (i = 0; i < found_n; i++) {
		bitarray_set_bit(bitmap, bloques[i] - 1);
	}

	actualizar_archivo_bitmap();

	return bloques;
}

void establecerConfiguracion(void) {
	if (config_has_property(config, "PUERTO_KERNEL")) {
		FSConfig.PUERTO_KERNEL = config_get_int_value(config, "PUERTO_KERNEL");
		logearInfo("Puerto Kernel: %d", FSConfig.PUERTO_KERNEL);
	}
	else {
		logearError("Error al leer el puerto del Kernel", true);
	}

	if (config_has_property(config, "IP_KERNEL")) {
		strcpy(FSConfig.IP_KERNEL, config_get_string_value(config, "IP_KERNEL"));
		logearInfo("IP Kernel: %s", FSConfig.IP_KERNEL);
	}
	else {
		logearError("Error al leer la IP del Kernel", true);
	}

	if (config_has_property(config, "PUERTO_MEMORIA")) {
		FSConfig.PUERTO_MEMORIA = config_get_int_value(config, "PUERTO_MEMORIA");
		logearInfo("Puerto Memoria: %d", FSConfig.PUERTO_MEMORIA);
	}
	else {
		logearError("Error al leer el puerto de la Memoria", true);
	}

	if (config_has_property(config, "PUNTO_MONTAJE")) {
		strcpy(FSConfig.PUNTO_MONTAJE, config_get_string_value(config, "PUNTO_MONTAJE"));
		logearInfo("Punto de Montaje: %s", FSConfig.PUNTO_MONTAJE);
	}
	else {
		logearError("Error al leer el punto de montaje del FS", true);
	}
}
