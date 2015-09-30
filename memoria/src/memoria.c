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
#include <sys/time.h>
#include <errno.h>

#include <commons/socket.h>
#include <commons/config.h>

#define BACKLOG 5			// Define cuantas conexiones vamos a mantener pendientes al mismo tiempo
#define PACKAGESIZE 1024	// Define cual va a ser el size maximo del paquete a enviar

typedef struct
{
	int mensajeSize;
	char mensaje[PACKAGESIZE];
}t_mensaje1;

int tamanioEstructura(t_mensaje1 unaPersona){

return    (sizeof(unaPersona.mensajeSize));

};

void recibirConexiones(char * PUERTO_CPU);
void procesar(int socket_recv, int socket_send);

int main()
{
	printf("\n");
	printf("----MEMORIA----\n\n");


	char * IP;

	t_config* config;

	config = config_create("home/utnso/git/tp-2015-2c-daft-punk-so/memoria/config.cfg");

	char * PUERTO_CPU = config_get_string_value(config, "PUERTO_CPU");

	recibirConexiones(PUERTO_CPU);


	IP = config_get_string_value(config, "IP_SWAP");
	char * PUERTO_SWAP = config_get_string_value(config, "PUERTO_SWAP");



	int socketSwap = conectarse(IP,PUERTO_SWAP);

	printf("Conectado a Swap :D\n");




	close(socketSwap);


	config_destroy(config);

	return 0;
}

void recibirConexiones(char * PUERTO_CPU)
{
	fd_set readset, tempset;
	int maxfd;
	int socketCPU, j, result;

	t_mensaje1 mensaje;
	void* package = malloc(tamanioEstructura(mensaje));

	int listenningSocket = recibirLlamada(PUERTO_CPU);

	printf("Esperando llamada! \n");

	FD_ZERO(&readset);
	FD_SET(listenningSocket, &readset);
	maxfd = listenningSocket;


	do
	{
	   memcpy(&tempset, &readset, sizeof(tempset));
	   result = select(maxfd + 1, &tempset, NULL, NULL, NULL);

	   if (result == 0)
	   {
		  printf("select() timed out!\n");
	   }
	   else if (result < 0 && errno != EINTR)
	   {
		  printf("Error in select(): %s\n", strerror(errno));
	   }
	   else if (result > 0)
	   {

		  if (FD_ISSET(listenningSocket, &tempset))
		  {
			  socketCPU = aceptarLlamada(listenningSocket);
			  printf("Conectado al CPU, urra!\n\n");


			  if (socketCPU < 0)
			  {
				  printf("Error in accept(): %s\n", strerror(errno));
			  }
			  else
			  {
				  FD_SET(socketCPU, &readset);
				  maxfd = (maxfd < socketCPU)?socketCPU:maxfd;
			  }

			  FD_CLR(listenningSocket, &tempset);

		  }
		  for (j=0; j<maxfd+1; j++)
		  {
			  if (FD_ISSET(j, &tempset))
			  {

				  do
				  {
					  result = recv(socketCPU, (void*)package, sizeof(mensaje.mensajeSize), 0);
				  }while (result == -1 && errno == EINTR);

				  if (result > 0)
				  {
					  memcpy(&mensaje.mensajeSize,package,sizeof(mensaje.mensajeSize));

					  void* package2=malloc(mensaje.mensajeSize);

					  recv(socketCPU,(void*) package2, mensaje.mensajeSize, 0);
					  memcpy(&mensaje.mensaje, package2,mensaje.mensajeSize);


					  printf("Longitud: %d \n",mensaje.mensajeSize);
					  printf("Mensaje: %s",mensaje.mensaje);
					  printf("\n");

					  free(package2);

				   }

				  else if (result == 0)
				  {
					  close(j);
					  FD_CLR(j, &readset);
				  }
				  else
				  {
					  printf("Error in recv(): %s\n", strerror(errno));
				  }
			  }      // end if (FD_ISSET(j, &tempset))
			  }      // end for (j=0;...)
		  }      // end else if (result > 0)
	   } while (1);

	free(package);

	close(socketCPU);

	close(listenningSocket);
}


void procesar(int socket_recv, int socket_send)
{
	char mensaje[PACKAGESIZE];

	recv(socket_recv, (void*) mensaje, PACKAGESIZE, 0);
	printf("%s\n",mensaje);

	send(socket_send, mensaje, strlen(mensaje) + 1, 0);
}
