#include "op_cpu.h"
#include "qepd/qepd.h"

int cpu_processar_operacion(int socket) {
	headerDeLosRipeados header;
	int ret = recibir_header(socket ,&header);

	if (ret <= 0) {
		return -1;
	}

	char operacion = header.codigoDeOperacion;

	switch (operacion) {
	case OBTENER_VALOR_VARIABLE:
		return cpu_obtener_variable(socket);

	case ASIGNAR_VALOR_VARIABLE:
		return 0;

	case INSTRUCCION:
		return 0;

	default:
		return -2;
	}
}

int cpu_obtener_variable(int socket) {

	posicionDeMemoriaVariable posicion; //Estructura del segundo send del CPU

	int ret = recv(socket, &posicion, sizeof posicion, 0);

	if (ret <= 0) {
		return -1;
	}

	if (buscarEnMemoriaYDevolver(posicion, socket) == -1) {

		printf("No se encontro en memoria"); //Habria que avisar al cpu
	}

}

void enviarDesdeCache(int indice, int socket, posicionDeMemoriaVariable posicion) {

	int posicionInicioACopiar = tablaAdministrativa_cache[indice].contenidoDeLaPag + posicion.start;
	int *buffer = malloc(sizeof (int));

	//memcpy(ir_a_frame_cache(indice) + posicion.start, buffer ,(posicion.offset + 1) - posicion.start);	En este memcpy uso la funcion ir a frame cache que me evita tener el offset (contenidoDePag) en la estructura

	memcpy(buffer, memoria_cache + posicionInicioACopiar ,(posicion.offset + 1) - posicion.start); // En este memcpy uso el contenidoDePag, no le veo la necesidad

	send(socket, buffer, 4, 0);





}


