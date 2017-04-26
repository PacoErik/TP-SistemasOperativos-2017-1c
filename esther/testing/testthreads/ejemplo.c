#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_THREADS 30
//Cada hilo va a sumar 10000 (unidad por unidad) a la variable j

#define USAR_SEMAFORO_MUTEX 0
//1: Los hilos van a hacer uso del mutex, cuando un hilo bloquea el mutex
//los demas esperan a que se desbloquee, osea, solo un hilo a la vez puede
//ejecutar lo que se llama seccion critica

//0: Los hilos no van a sincronizarse, ver que el resultado final de la
//variable j va a variar de ejecucion en ejecucion

pthread_mutex_t mutex;
void* imprimir(void*);
int j=0;

int main(void) {
	pthread_mutex_init(&mutex,NULL);
	pthread_t hilo[MAX_THREADS];
	int i;
	for (i=0;i<MAX_THREADS;i++) {
		//Crear hilos y almacenarlos en el array de hilos
		//Vamos a pasarle por parametro la "ID del hilo"
		pthread_create(&hilo[i],NULL,&imprimir,&i);
	}
	for (i=0;i<MAX_THREADS;i++) {
		//Esperar a todos los hilos que terminen
		pthread_join(hilo[i],NULL);
	}
	//Luego de haber hecho nuestro trabajo con los hilos
	//destruimos el semaforo que no vamos a volver a usar
	pthread_mutex_destroy(&mutex);
	return EXIT_SUCCESS;
}

void* imprimir(void* b) {
	int *a = b;
	int i;
	for (i=0;i<10000;i++) {
		//En este caso, la seccion critica es en la que se modifica
		//la variable global (j): "j++;"

		if (USAR_SEMAFORO_MUTEX) {
			pthread_mutex_lock(&mutex);
			j++;
			pthread_mutex_unlock(&mutex);
		} else {
			//En el caso en el que no se use el semaforo mutex, estos hilos pueden acceder
			//simultaneamente a la ejecucion de este bloque de codigo,
			//llamado seccion critica, y se da una condicion de carrera
			j++;
		}

		printf("Hilo %i: j=%i\n",*a,j);
	}
	//Si estan sincronizados, el resultado final debe ser 10000*MAX_THREADS
	//Si no lo estan, este resultado puede variar
	return NULL;
}
