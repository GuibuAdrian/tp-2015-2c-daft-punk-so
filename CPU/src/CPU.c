/*
 ============================================================================
 Name        : CPU.c
 Author      : 
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>

#include <commons/socket.h>
#include <commons/config.h>
#include <commons/collections/list.h>

#define BACKLOG 5			// Define cuantas conexiones vamos a mantener pendientes al mismo tiempo
#define PACKAGESIZE 1024	// Define cual va a ser el size maximo del paquete a enviar

typedef struct {
	int planificador;
	int memoria;
} t_socket;

void procesar(int socket_recv, int socket_send) {
	char mensaje[PACKAGESIZE];

	recv(socket_recv, (void*) mensaje, PACKAGESIZE, 0);
	printf("%s\n", mensaje);

	send(socket_send, mensaje, strlen(mensaje) + 1, 0);
}

int main() {
	printf("\n");
	printf("----CPU----\n\n");
	char ruta[PACKAGESIZE];
	t_socket socket;
	t_config* config;

	//ABRO ARCH DE CONF
	//TODO: pasar el archivo de configuracion de una forma no tan hardcode
	config = config_create("/home/utnso/github/tp-2015-2c-daft-punk-so/CPU/config.cfg");
	char *IP_Planificador = config_get_string_value(config, "IP_PLANIFICADOR");
	char *PUERTO_PLANIFICADOR = config_get_string_value(config,"PUERTO_PLANIFICADOR");
	int cant_hilos = config_get_int_value(config, "CANTIDAD_HILOS");
	char *PUERTO_MEMORIA = config_get_string_value(config, "PUERTO_MEMORIA");
	char *IP_Memoria = config_get_string_value(config, "IP_MEMORIA");
	t_list *listaDeHilos = list_create(); //creamos la lista de hilos D:

	int i;
	for ( i = 0; i < cant_hilos; i++) {
		phtread_t hilo;
		phtread_create( &hilo, NULL, conexion, arg); //TODO: hacer la funcion de conexion de cada CPU y una estructura que sea nuestro argumento.
		list_add(listaDeHilos, hilo);
	}
	//TODO: tomar el conectarse de planificador y de memoria y meterlos en una unica funcion de conexion
	//Me conecto al PLANIFICADOR
	socket.planificador = conectarse( IP_Planificador, PUERTO_PLANIFICADOR);

	//Me conecto a MEMORIA
	socket.memoria = conectarse( IP_Memoria, PUERTO_MEMORIA);

	/*
	 pthread_t unHilo;
	 pthread_create(&unHilo,NULL,(void*) procesar, (void*)&socket);
	 */
	procesar(socket.planificador, socket.memoria);

	close(socket.planificador);
	close(socket.memoria);
	config_destroy(config);
	//aca cerramos los hilos por si las moscas y liberamos la lista de CPU's
	for ( i = 0; i < cant_hilos; i++) {
		phtread_join( hilo, NULL);
	}
	list_clean( listaDeHilos);
	return 0;
}
