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
#include <commons/collections/list.h>

#define BACKLOG 5			// Define cuantas conexiones vamos a mantener pendientes al mismo tiempo
#define PACKAGESIZE 1024	// Define cual va a ser el size maximo del paquete a enviar

void procesar(int socket_recv, int socket_send) {
	char mensaje[PACKAGESIZE];

	recv(socket_recv, (void*) mensaje, PACKAGESIZE, 0);
	printf("%s\n", mensaje);

	send(socket_send, mensaje, strlen(mensaje) + 1, 0);
}

typedef struct {
	int pid;
	int pc;
	char *status;//puedde ser listo, ejecutando o bloqueado.
	char *codigo;
	int numeroDeInstrucciones;
} t_pcb;

typedef struct {
	int pid;
	int pagina;
	int marco;
	char modificada;//indica si la pagina fue o no modificada.
	char escritura;//indica si se puede escribir la pagina
	char lectura;//indica si se puede leer la pagina
	//a tener en cuenta si una pagina se escribe hay que modificar el estado modificada
} t_pagina;

int main() {
	printf("\n");
	printf("----MEMORIA----\n\n");

	char * IP_SWAP;
	t_config* config;
	t_list tablaDeProcesos = list_create();
	t_list tablaDePaginas = list_create();

	config = config_create(
			"/home/utnso/github/tp-2015-2c-daft-punk-so/memoria/config.cfg");

	char * PUERTO_CPU = config_get_string_value(config, "PUERTO_CPU");

	int listenningSocket = recibirLlamada(PUERTO_CPU);

	printf("Esperando llamada! \n");

	int socketCPU = aceptarLlamada(listenningSocket);

	printf("Conectado al CPU, urra!\n");
	t_pcb pcbProceso;
	//hay que recibir datos para armar la pcb de un proceso y luego agregamos a la tabla de procesos
	list_add(tablaDeProcesos, pcbProceso);
	//al igual que con el pcb hay que pasarle los parametros necesarios que nos indique el CPU
	t_pagina pagina;
	list_add(tablaDePaginas, pagina);

	IP_SWAP = config_get_string_value(config, "IP_SWAP");
	char * PUERTO_SWAP = config_get_string_value(config, "PUERTO_SWAP");

	int socketSwap = conectarse(IP_SWAP, PUERTO_SWAP);

	printf("Conectado a Swap :D\n");

	procesar(socketCPU, socketSwap);

	close(socketCPU);

	close(socketSwap);
	close(listenningSocket);

	config_destroy(config);

	return 0;
}
