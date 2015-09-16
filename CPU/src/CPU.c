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

#define BACKLOG 5			// Define cuantas conexiones vamos a mantener pendientes al mismo tiempo
#define PACKAGESIZE 1024	// Define cual va a ser el size maximo del paquete a enviar



typedef struct
{
	int planificador;
	int memoria;
} t_socket;



void procesar(int socket_recv, int socket_send)
{
	char mensaje[PACKAGESIZE];

	recv(socket_recv, (void*) mensaje, PACKAGESIZE, 0);
	printf("%s\n",mensaje);

	send(socket_send, mensaje, strlen(mensaje) + 1, 0);
}



int main()
{
	printf("\n");
	printf("----CPU----\n\n");



	char ruta[PACKAGESIZE];

	t_socket socket;

	t_config* config;


	//ABRO ARCH DE CONF
	config = config_create("/home/utnso/github/tp-2015-2c-daft-punk-so/CPU/config.cfg");


	char * IP = config_get_string_value(config, "IP_PLANIFICADOR");
	char * PUERTO_PLANIFICADOR = config_get_string_value(config, "PUERTO_PLANIFICADOR");

	//Me conecto al PLANIFICADOR
	socket.planificador = conectarse(IP,PUERTO_PLANIFICADOR);



	IP = config_get_string_value(config, "IP_MEMORIA");
	char * PUERTO_MEMORIA = config_get_string_value(config, "PUERTO_MEMORIA");

	//Me conecto a MEMORIA
	socket.memoria = conectarse(IP,PUERTO_MEMORIA);




	int cant_hilos = config_get_int_value(config, "CANTIDAD_HILOS");


/*
	pthread_t unHilo;
	pthread_create(&unHilo,NULL,(void*) procesar, (void*)&socket);
*/
	procesar(socket.planificador,socket.memoria);


	close(socket.planificador);

	close(socket.memoria);

    config_destroy(config);

	return 0;
}
