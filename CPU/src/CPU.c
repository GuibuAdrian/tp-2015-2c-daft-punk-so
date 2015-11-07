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

#include <commons/collections/list.h>
#include <commons/socket.h>
#include <commons/config.h>
#include <commons/txt.h>
#include <commons/log.h>

#define BACKLOG 5			// Define cuantas conexiones vamos a mantener pendientes al mismo tiempo
#define PACKAGESIZE 1024	// Define cual va a ser el size maximo del paquete a enviar

typedef struct {
	int idNodo;
	int cantHilos;
} t_idHilo;

typedef struct {
	int pid;
	int pathSize;
	char path[PACKAGESIZE];
	int puntero;
} t_pathMensaje;

typedef struct
{
	int pid;
	int orden;	// 0=Iniciar, 1=Leer, 2=Escribir, 3=Finalizar
	int pagina;
	int contentSize;
	char content[PACKAGESIZE];
}t_orden_CPU;

typedef struct
{
	pthread_t unHilo;
} t_hilos;

typedef struct
{
	int pathSize;
	char path[PACKAGESIZE];
} t_mensaje;

t_log* logger;
t_list *listaHilos;
char *ipPlanificador, *puertoPlanificador, *ipMemoria, *puertoMemoria;
int id = 5, RETARDO, numeroHilos;

static t_hilos *hilo_create(pthread_t unHilo);
static void hilo_destroy(t_hilos *self);
int tamanioEstructura1(t_pathMensaje unaPersona);
int tamanioEstructura2(t_pathMensaje unaPersona);
int tamanioMensajeMemo(t_orden_CPU mensajeMemo);
void conectarHilos1();
void recibirPath1(int serverSocket);
char * obtenerLinea(char path[PACKAGESIZE], int puntero);
void interpretarLinea(int socketPlanificador, char* linea, int pid);
char * obtenerLinea(char path[PACKAGESIZE], int puntero);
t_orden_CPU enviarOrdenAMemoria(int pid, int orden, int paginas, char *content);

int main()
{
	printf("\n");
	printf("~~~~~~~~~~CPU~~~~~~~~~~\n\n");


	listaHilos = list_create();


	logger = log_create("logsTP", "CPU", true, LOG_LEVEL_INFO);

	t_config* config;

	config = config_create("config.cfg");

	ipPlanificador = config_get_string_value(config, "IP_PLANIFICADOR");
	puertoPlanificador = config_get_string_value(config, "PUERTO_PLANIFICADOR");
	ipMemoria = config_get_string_value(config, "IP_MEMORIA");
	puertoMemoria = config_get_string_value(config, "PUERTO_MEMORIA");
	numeroHilos = config_get_int_value(config, "CANTIDAD_HILOS");
	RETARDO = config_get_int_value(config, "RETARDO");


	int i = 1;
	pthread_t unHilo;

	while (i <= numeroHilos)
	{
		pthread_create(&unHilo, NULL, (void*) conectarHilos1, NULL);

		list_add(listaHilos,hilo_create(unHilo));

		i++;
	}

	int j;
	t_hilos* new;

	for(j=0; j<list_size(listaHilos); j++)
	{
		new = list_get(listaHilos,j);

		pthread_join(new->unHilo, NULL);
	}


	list_destroy_and_destroy_elements(listaHilos,(void*) hilo_destroy);

	config_destroy(config);
	log_info(logger, "---------------------FIN---------------------");
	log_destroy(logger);

	return 0;
}

void conectarHilos1(void *context)
{
	int serverSocket;
	serverSocket = conectarse(ipPlanificador, puertoPlanificador);

	log_info(logger,"Planificador: %d conectado, viva!!", serverSocket);

	t_idHilo mensaje;

	mensaje.idNodo = serverSocket;
	mensaje.cantHilos = numeroHilos;

	//Envio id y aviso la cantidad de conexiones
	void* package = malloc(sizeof(t_idHilo));

	memcpy(package, &mensaje.idNodo, sizeof(mensaje.idNodo));
	memcpy(package + sizeof(mensaje.idNodo), &mensaje.cantHilos, sizeof(mensaje.cantHilos));

	send(serverSocket, package, sizeof(t_idHilo), 0);

	free(package);

	recibirPath1(serverSocket);

	close(serverSocket);

}

void recibirPath1(int serverSocket) {
	int status = 1;

	t_pathMensaje unaPersona;

	void* package = malloc(tamanioEstructura2(unaPersona));

	while (status)
	{
		status = recv(serverSocket, (void*) package, sizeof(unaPersona.pid), 0);

		if (status == 0)
		{
			break;
		}
		else
		{
			memcpy(&unaPersona.pid, package, sizeof(unaPersona.pid));
			recv(serverSocket, (void*) (package + sizeof(unaPersona.pid)), sizeof(unaPersona.puntero), 0);
			memcpy(&unaPersona.puntero, package + sizeof(unaPersona.pid), sizeof(unaPersona.puntero));	//--
			recv(serverSocket, (void*) (package + sizeof(unaPersona.pid) + sizeof(unaPersona.puntero)), sizeof(unaPersona.pathSize), 0);
			memcpy(&unaPersona.pathSize, package + sizeof(unaPersona.pid) + sizeof(unaPersona.puntero), sizeof(unaPersona.pathSize));	//--

			void* package2 = malloc(unaPersona.pathSize);

			recv(serverSocket, (void*) package2, unaPersona.pathSize, 0);
			memcpy(&unaPersona.path, package2, unaPersona.pathSize);

			char * linea = obtenerLinea(unaPersona.path, unaPersona.puntero);

			log_info(logger,"~~~~~~~~~~~~~~~~~~~Socket: %d...mProc: %d~~~~~~~~~~~~~~~~~~~", serverSocket, unaPersona.pid);

			strncpy(unaPersona.path, " ", PACKAGESIZE);

			interpretarLinea(serverSocket,linea, unaPersona.pid);

			sleep(RETARDO);

			free(linea);
			free(package2);
		}
	}

	free(package);
}


t_orden_CPU recibirRespuestaSwap(int socketMemoria)
{
	t_orden_CPU mensajeSwap;

	void* package = malloc(	sizeof(mensajeSwap.pid) + sizeof(mensajeSwap.orden) + sizeof(mensajeSwap.pagina) + sizeof(mensajeSwap.contentSize));

	recv(socketMemoria, (void*) package, sizeof(mensajeSwap.pid), 0);
	memcpy(&mensajeSwap.pid, package, sizeof(mensajeSwap.pid));
	recv(socketMemoria, (void*) (package + sizeof(mensajeSwap.pid)), sizeof(mensajeSwap.orden), 0);
	memcpy(&mensajeSwap.orden, package + sizeof(mensajeSwap.pid), sizeof(mensajeSwap.orden));	//--
	recv(socketMemoria, (void*) (package + sizeof(mensajeSwap.pid) + sizeof(mensajeSwap.orden)), sizeof(mensajeSwap.pagina), 0);
	memcpy(&mensajeSwap.pagina, package + sizeof(mensajeSwap.pid) + sizeof(mensajeSwap.orden), sizeof(mensajeSwap.pagina));	//--
	recv(socketMemoria,(void*) (package + sizeof(mensajeSwap.pid) + sizeof(mensajeSwap.orden) + sizeof(mensajeSwap.pagina)), sizeof(mensajeSwap.contentSize), 0);//--
	memcpy(&mensajeSwap.contentSize,(void*) package + sizeof(mensajeSwap.pid) + sizeof(mensajeSwap.orden) + sizeof(mensajeSwap.pagina), sizeof(mensajeSwap.contentSize));

	void* package2=malloc(mensajeSwap.contentSize);

	recv(socketMemoria, (void*) package2, mensajeSwap.contentSize, 0);//campo longitud(NO SIZEOF DE LONGITUD)
	memcpy(&mensajeSwap.content, package2, mensajeSwap.contentSize);

	free(package);
	free(package2);

	return mensajeSwap;
}

void enviarRespuestaPlanificador(int socketPlanificador, int pid, int orden, int pagina, char *content)
{
	t_orden_CPU respuestaPlan;
	respuestaPlan.pid = pid;
	respuestaPlan.orden = orden;
	respuestaPlan.pagina = pagina;
	respuestaPlan.contentSize = strlen(content)+1;
	strcpy(respuestaPlan.content, content);

	void* respuestaPackage = malloc(tamanioMensajeMemo(respuestaPlan));

	memcpy(respuestaPackage, &respuestaPlan.pid, sizeof(respuestaPlan.pid));
	memcpy(respuestaPackage+sizeof(respuestaPlan.pid), &respuestaPlan.orden, sizeof(respuestaPlan.orden));
	memcpy(respuestaPackage+sizeof(respuestaPlan.pid)+sizeof(respuestaPlan.orden), &respuestaPlan.pagina, sizeof(respuestaPlan.pagina));
	memcpy(respuestaPackage+sizeof(respuestaPlan.pid)+sizeof(respuestaPlan.orden)+sizeof(respuestaPlan.pagina), &respuestaPlan.contentSize, sizeof(respuestaPlan.contentSize));
	memcpy(respuestaPackage+sizeof(respuestaPlan.pid)+sizeof(respuestaPlan.orden)+sizeof(respuestaPlan.pagina)+sizeof(respuestaPlan.contentSize), &respuestaPlan.content, respuestaPlan.contentSize);

	log_info(logger, "Respuesta %d", respuestaPlan.orden);

	send(socketPlanificador, respuestaPackage, tamanioMensajeMemo(respuestaPlan), 0);

	free(respuestaPackage);
}

t_orden_CPU enviarOrdenAMemoria(int pid, int orden, int paginas, char *content)
{
	t_orden_CPU mensajeMemoria;

	mensajeMemoria.pid = pid;
	mensajeMemoria.orden = orden;
	mensajeMemoria.pagina = paginas;
	mensajeMemoria.contentSize = strlen(content)+1;
	strcpy(mensajeMemoria.content,content);

	int socketMemoria = conectarse(ipMemoria, puertoMemoria);

	void* mensajeMemoPackage = malloc(tamanioMensajeMemo(mensajeMemoria));

	memcpy(mensajeMemoPackage, &mensajeMemoria.pid, sizeof(mensajeMemoria.pid));
	memcpy(mensajeMemoPackage+sizeof(mensajeMemoria.pid), &mensajeMemoria.orden, sizeof(mensajeMemoria.orden));
	memcpy(mensajeMemoPackage+sizeof(mensajeMemoria.pid)+sizeof(mensajeMemoria.orden), &mensajeMemoria.pagina, sizeof(mensajeMemoria.pagina));
	memcpy(mensajeMemoPackage+sizeof(mensajeMemoria.pid)+sizeof(mensajeMemoria.orden)+sizeof(mensajeMemoria.pagina), &mensajeMemoria.contentSize, sizeof(mensajeMemoria.contentSize));
	memcpy(mensajeMemoPackage+sizeof(mensajeMemoria.pid)+sizeof(mensajeMemoria.orden)+sizeof(mensajeMemoria.pagina)+sizeof(mensajeMemoria.contentSize), &mensajeMemoria.content, mensajeMemoria.contentSize);

	send(socketMemoria, mensajeMemoPackage, tamanioMensajeMemo(mensajeMemoria), 0);

	free(mensajeMemoPackage);

	return recibirRespuestaSwap(socketMemoria);
}

void interpretarLinea(int socketPlanificador, char* linea, int pid)
{
	char * pch = strtok(linea, " \n");
	int pagina;
	t_orden_CPU mensaje;

	if (strncmp(pch, "iniciar", 7) == 0)
	{
		pch = strtok(NULL, " \n");
		pagina = strtol(pch, NULL, 10);

		mensaje = enviarOrdenAMemoria(pid, 0, pagina, "/");
	}
	else
	{
		if (strncmp(pch, "leer", 5) == 0)
		{
			pch = strtok(NULL, " \n");
			pagina = strtol(pch, NULL, 10);

			mensaje = enviarOrdenAMemoria(pid, 1, pagina, "/");

			printf("Mensaje: %s\n", mensaje.content);
		}
		else
		{
			if (strncmp(pch, "finalizar", 9) == 0)
			{
				mensaje = enviarOrdenAMemoria(pid, 3, 0, "/");
			}
			else
			{
				if (strncmp(pch, "escribir", 9) == 0)
				{
					pch = strtok(NULL, " \n");
					pagina = strtol(pch, NULL, 10);

					pch = strtok(NULL, " \n");

					mensaje = enviarOrdenAMemoria(pid, 2, pagina, pch);
				}
			}
		}
	}
	enviarRespuestaPlanificador(socketPlanificador, mensaje.pid, mensaje.orden, mensaje.pagina, mensaje.content);
}

char * obtenerLinea(char path[PACKAGESIZE], int puntero) {
	FILE* file;
	file = txt_open_for_read(path);
	char * linea = read_line(file, puntero - 1);

	txt_close_file(file);

	return linea;
}

int tamanioEstructura1(t_pathMensaje unaPersona) {
	return (sizeof(unaPersona.puntero) + sizeof(unaPersona.pathSize)
			+ sizeof(unaPersona.pid));
};

int tamanioMensajeMemo(t_orden_CPU mensajeMemo)
{
	return (sizeof(mensajeMemo.pid)+sizeof(mensajeMemo.pagina)+sizeof(mensajeMemo.orden)+sizeof(mensajeMemo.contentSize)+mensajeMemo.contentSize);
};
int tamanioEstructura2(t_pathMensaje unaPersona) {
	return (sizeof(unaPersona.puntero) + sizeof(unaPersona.pathSize)
			+ sizeof(unaPersona.pid));
};
static t_hilos *hilo_create(pthread_t unHilo)
{
	 t_hilos *new = malloc(sizeof(t_hilos));

	 new->unHilo=unHilo;

	 return new;
}
static void hilo_destroy(t_hilos *self)
{
    free(self);
}
