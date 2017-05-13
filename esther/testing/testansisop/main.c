#include <stdio.h>
#include <parser/metadata_program.h>
#include <stdlib.h>
#include <commons/collections/dictionary.h>

int main() {
	FILE* prog = fopen("programas/stackoverflow.ansisop","r");
	char* codigo = NULL;
	long int bytes;
	int i;

	fseek(prog, 0, SEEK_END);
	bytes = ftell(prog);
	fseek(prog, 0, SEEK_SET);
	codigo = malloc(bytes);
	fread(codigo, 1, bytes, prog);
	fclose(prog);

	t_metadata_program *info = metadata_desde_literal(codigo);

	printf("Código: %s\n",codigo);
	printf("Instrucción inicio: %i\n",info->instruccion_inicio);
	printf("Cantidad de instrucciones: %i\n",info->instrucciones_size);
	for (i=0;i<info->instrucciones_size;i++) {
		t_intructions instruccion = info->instrucciones_serializado[i];
		printf("Instrucción [%i] = Start: %i Offset: %i\n",i,instruccion.start,instruccion.offset);
	}
	int cantidad_etiquetas = info->cantidad_de_etiquetas;
	printf("Tamaño buffer etiquetas: %i\n",info->etiquetas_size);
	printf("Cantidad etiquetas: %i\n",cantidad_etiquetas);
	printf("Cantidad de funciones: %i\n",info->cantidad_de_funciones);

	t_puntero_instruccion puntero_instruccion;

	//Esto es lo que haría el CPU en sus primitivas que usan las etiquetas
	//metadata_buscar_etiqueta devuelve el valor que debería tener el PC
	//para ejecutar la próxima instrucción (que estaría dentro de una función de Ansisop)
	//En otras palabras, devuelve el número de la instrucción a ejecutar.
	//Devuelve -1 si no encuentra la etiqueta.
	puntero_instruccion = metadata_buscar_etiqueta("f",info->etiquetas,info->etiquetas_size);
	printf("Puntero a instruccion de etiqueta %s: %i\n","f",puntero_instruccion);

	puntero_instruccion = metadata_buscar_etiqueta("g",info->etiquetas,info->etiquetas_size);
	printf("Puntero a instruccion de etiqueta %s: %i\n","g",puntero_instruccion);

	metadata_destruir(info);
	free(codigo);
	return 0;
}
