/*
 ============================================================================
 Name        : Planificador.c
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




void procesar(int socket)
{
	char mensaje[PACKAGESIZE];


	printf("Mandar mensaje: ");
	fgets(mensaje, PACKAGESIZE, stdin);

	send(socket, mensaje, strlen(mensaje) + 1, 0);

};


int main()
{
	printf("\n");
	printf("----PLANIFICADOR----\n\n");


	t_config* config;

	config = config_create("/home/utnso/github/tp-2015-2c-daft-punk-so/planificador/config.cfg");

	char * PUERTO_ESCUCHA = config_get_string_value(config, "PUERTO_ESCUCHA");





	int listenningSocket = recibirLlamada(PUERTO_ESCUCHA);

	printf("Esperando llamada! \n");

	int socketCPU = aceptarLlamada(listenningSocket);

	printf("¡CPU conectado! \n");





	procesar(socketCPU);




	close(socketCPU);
	close(listenningSocket);


    config_destroy(config);

	return 0;
}
