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
#include <commons/log.h>


#define BACKLOG 5			// Define cuantas conexiones vamos a mantener pendientes al mismo tiempo
#define PACKAGESIZE 1024	// Define cual va a ser el size maximo del paquete a enviar


typedef struct
{
	int pid;
	int orden;	// 0=Iniciar, 1=Leer, 2=Escribir, 3=Finalizar
	int pagina;
}t_orden_memoria;

typedef struct
{
	int pid;
	int paginas;
	int mensajeSize;
}t_respuesta_CPU;

int socketSwap;
t_log* logger;

int tamanioEstructura(t_orden_memoria unaPersona);
int tamanioRespuestaCPU(t_respuesta_CPU unaPersona);

void recibirConexiones(char * PUERTO_CPU);
void procesar(t_orden_memoria mensaje, int socketCPU);
void recibirRespuestaSwap(int SocketCPU);
void EnviarRespuestaCPU(void* respuestaPackage, t_respuesta_CPU respuestaMemoria, int socketCPU);
int tamanioRespuestaMemoria(t_respuesta_CPU unaPersona);

int main()
{
	printf("\n");
	printf("----MEMORIA----\n\n");


	logger = log_create("/home/utnso/github/tp-2015-2c-daft-punk-so/memoria/logsTP", "MEMORIA",true, LOG_LEVEL_INFO);


	char * IP;

	t_config* config;

	config = config_create("/home/utnso/github/tp-2015-2c-daft-punk-so/memoria/config.cfg");

	IP = config_get_string_value(config, "IP_SWAP");
	char * PUERTO_SWAP = config_get_string_value(config, "PUERTO_SWAP");

	socketSwap = conectarse(IP,PUERTO_SWAP);

	log_info(logger, "Conectado a Swap :D");


	char * PUERTO_CPU = config_get_string_value(config, "PUERTO_CPU");

	recibirConexiones(PUERTO_CPU);


	log_info(logger, "-------------------------------------------------");

	close(socketSwap);

	config_destroy(config);
    log_destroy(logger);

	return 0;
}

void recibirConexiones(char * PUERTO_CPU)
{
	fd_set readset, tempset;
	int maxfd;
	int socketCPU, j, result;

	t_orden_memoria mensaje;
	void* package = malloc(tamanioEstructura(mensaje));

	int listenningSocket = recibirLlamada(PUERTO_CPU);

	FD_ZERO(&readset);
	FD_SET(listenningSocket, &readset);
	maxfd = listenningSocket;


	do
	{
	   memcpy(&tempset, &readset, sizeof(tempset));
	   result = select(maxfd + 1, &tempset, NULL, NULL, NULL);

	   if (result == 0)
	   {
		  log_error(logger, "Error in select() timed out!");
	   }
	   else if (result < 0 && errno != EINTR)
	   {
		   log_error(logger, "Error in select(): %s", strerror(errno));
	   }
	   else if (result > 0)
	   {

		  if (FD_ISSET(listenningSocket, &tempset))
		  {
			  socketCPU = aceptarLlamada(listenningSocket);

			  log_info(logger, "Conectado al CPU, urra!");


			  if (socketCPU < 0)
			  {
				  log_error(logger, "Error in accept(): %s", strerror(errno));
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

				  log_info(logger, "Esperando orden");

				  do
				  {
					  result = recv(socketCPU, (void*)package, sizeof(mensaje.pid), 0);

				  }while (result == -1 && errno == EINTR);

				  if (result > 0)
				  {
					  memcpy(&mensaje.pid,package,sizeof(mensaje.pid));

					  recv(socketCPU,(void*) (package+sizeof(mensaje.pid)), sizeof(mensaje.pagina), 0);

					  memcpy(&mensaje.orden, package+sizeof(mensaje.pid),sizeof(mensaje.orden));

					  recv(socketCPU,(void*) (package+sizeof(mensaje.pid)+sizeof(mensaje.orden)), sizeof(mensaje.pagina), 0);

					  memcpy(&mensaje.pagina, package+sizeof(mensaje.pid)+sizeof(mensaje.orden),sizeof(mensaje.pagina));


					  log_info(logger, "mProc: %d.", mensaje.pid);
					  log_info(logger, "%d paginas", mensaje.pagina);
					  log_info(logger, "Orden %d", mensaje.orden);

					  procesar(mensaje, socketCPU);

				   }

				  else if (result == 0)
				  {
					  close(j);
					  FD_CLR(j, &readset);
				  }
				  else
				  {
					  log_error(logger, "Error in recv(): %s", strerror(errno));

				  }

			  }      // end if (FD_ISSET(j, &tempset))
			  }      // end for (j=0;...)

		  if (result==0)
		  {
			  break;
		  }

		  }      // end else if (result > 0)
	   } while (1);

	free(package);

	close(socketCPU);

	close(listenningSocket);
}


void procesar(t_orden_memoria mensaje, int socketCPU)
{

	void* ordenPackage = malloc( (sizeof(int)+sizeof(int)+sizeof(int)) );

	memcpy(ordenPackage,&mensaje.pid,sizeof(mensaje.pid));
	memcpy(ordenPackage+sizeof(mensaje.pid),&mensaje.orden,sizeof(mensaje.orden));
	memcpy(ordenPackage+sizeof(mensaje.pid)+sizeof(mensaje.pagina), &mensaje.pagina, sizeof(mensaje.pagina));



	send(socketSwap, ordenPackage, sizeof(int)+sizeof(int)+sizeof(int),0);

	log_info(logger, "Orden enviada");

	recibirRespuestaSwap(socketCPU);

	free(ordenPackage);

}

void recibirRespuestaSwap(int socketCPU)
{
	log_info(logger, "Esperando resultado");

	t_respuesta_CPU respuesta;

	void* package = malloc(sizeof(respuesta.pid)+sizeof(respuesta.paginas)+sizeof(respuesta.mensajeSize));

	recv(socketSwap,(void*)package, sizeof(respuesta.pid), 0);
	memcpy(&respuesta.pid,package,sizeof(respuesta.pid));

	recv(socketSwap,(void*) (package+sizeof(respuesta.pid)), sizeof(respuesta.paginas), 0);
	memcpy(&respuesta.paginas, package+sizeof(respuesta.pid),sizeof(respuesta.paginas));

	recv(socketSwap,(void*) (package+sizeof(respuesta.pid)+sizeof(respuesta.paginas)), sizeof(respuesta.mensajeSize), 0);
	memcpy(&respuesta.mensajeSize, package+sizeof(respuesta.pid)+sizeof(respuesta.paginas),sizeof(respuesta.mensajeSize));

	EnviarRespuestaCPU(package, respuesta, socketCPU);

	free(package);

}

void EnviarRespuestaCPU(void* respuestaPackage, t_respuesta_CPU respuestaMemoria, int socketCPU)
{
	memcpy(respuestaPackage,&respuestaMemoria.pid,sizeof(respuestaMemoria.pid));
	memcpy(respuestaPackage+sizeof(respuestaMemoria.pid),&respuestaMemoria.paginas,sizeof(respuestaMemoria.paginas));
	memcpy(respuestaPackage+sizeof(respuestaMemoria.pid)+sizeof(respuestaMemoria.paginas), &respuestaMemoria.mensajeSize, sizeof(respuestaMemoria.mensajeSize));

	send(socketCPU, respuestaPackage, tamanioRespuestaMemoria(respuestaMemoria),0);

}
int tamanioRespuestaCPU(t_respuesta_CPU unaPersona)
{
	return (sizeof(unaPersona.pid)+sizeof(unaPersona.paginas)+sizeof(unaPersona.mensajeSize));
};

int tamanioEstructura(t_orden_memoria unaPersona){

return    (sizeof(unaPersona.orden)+sizeof(unaPersona.pagina));

};

int tamanioRespuestaMemoria(t_respuesta_CPU unaPersona)
{
	return (sizeof(unaPersona.pid)+sizeof(unaPersona.paginas)+sizeof(unaPersona.mensajeSize));
};
