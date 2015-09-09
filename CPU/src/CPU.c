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

#include <commons/socket.h>
#include <commons/config.h>

#define BACKLOG 5			// Define cuantas conexiones vamos a mantener pendientes al mismo tiempo
#define PACKAGESIZE 1024	// Define cual va a ser el size maximo del paquete a enviar





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





	t_config* config;

	config = config_create("/home/utnso/github/tp-2015-2c-daft-punk-so/CPU/config.cfg");

	char * IP = config_get_string_value(config, "IP_PLANIFICADOR");
	char * PUERTO_PLANIFICADOR = config_get_string_value(config, "PUERTO_PLANIFICADOR");





	int socketPlanificador = conectarse(IP,PUERTO_PLANIFICADOR);

	printf("Conectado al Planificador, urra!\n");





	IP = config_get_string_value(config, "IP_MEMORIA");
	char * PUERTO_MEMORIA = config_get_string_value(config, "PUERTO_MEMORIA");



	int socketMemoria = conectarse(IP,PUERTO_MEMORIA);

	printf("Conectado a Memoria, wachin!\n");




	procesar(socketPlanificador, socketMemoria);



	close(socketPlanificador);

	close(socketMemoria);

    config_destroy(config);

	return 0;
}
