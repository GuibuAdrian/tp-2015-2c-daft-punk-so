/*
 ============================================================================
 Name        : memoria.c
 Author      : Daiana
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */
/*
 ============================================================================
 Name        : Memoria.c
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
	printf("----MEMORIA----\n\n");



	char * IP;

	t_config* config;

	config = config_create("/home/utnso/github/tp-2015-2c-daft-punk-so/memoria/config.cfg");

	char * PUERTO_CPU = config_get_string_value(config, "PUERTO_CPU");






	int listenningSocket = recibirLlamada(PUERTO_CPU);

	printf("Esperando llamada! \n");

	int socketCPU = aceptarLlamada(listenningSocket);

	printf("Conectado al CPU, urra!\n");





	IP = config_get_string_value(config, "IP_SWAP");
	char * PUERTO_SWAP = config_get_string_value(config, "PUERTO_SWAP");



	int socketSwap = conectarse(IP,PUERTO_SWAP);

	printf("Conectado a Swap :D\n");




	procesar(socketCPU, socketSwap);



	close(socketCPU);

	close(socketSwap);
	close(listenningSocket);


	config_destroy(config);

	return 0;
}

