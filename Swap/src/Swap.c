/*
 ============================================================================
 Name        : Swap.c
 Author      : Leandro
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

#define PACKAGESIZE 1024	// Define cual va a ser el size maximo del paquete a enviar

void procesar(int socket_recv)
{
	char mensaje[PACKAGESIZE];

	recv(socket_recv, (void*) mensaje, PACKAGESIZE, 0);
	printf("%s\n",mensaje);
};


int main()
{
	printf("\n");
	printf("----SWAP----\n\n");

	t_config* config;

	config = config_create("/home/utnso/git/tp-2015-2c-daft-punk-so/Swap/config.cfg");

	char * PUERTO_ESCUCHA = config_get_string_value(config, "PUERTO_ESCUCHA");
	char * IP = "192.168.1.40";

	int listenningSocket = recibirLlamada(IP,PUERTO_ESCUCHA);

	printf("Esperando llamada! \n");

	int socket_memoria = aceptarLlamada(listenningSocket);

	printf("Conectado a Memoria\n");

	procesar(socket_memoria);

	close(socket_memoria);
	close(listenningSocket);


	config_destroy(config);

	return 0;
}

