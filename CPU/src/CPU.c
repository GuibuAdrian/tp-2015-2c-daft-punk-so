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
#include <commons/txt.h>
#include <commons/log.h>
#include <commons/collections/list.h>

#define BACKLOG 5			// Define cuantas conexiones vamos a mantener pendientes al mismo tiempo
#define PACKAGESIZE 1024	// Define cual va a ser el size maximo del paquete a enviar

typedef struct {
	int idNodo;
	int cantHilos;
} t_idHilo;

typedef struct {
	char *ipPlanificador;
	char *puertoPlanificador;
	char *ipMemoria;
	char *puertoMemoria;
	int numeroHilos;
} tspaghetti;

typedef struct {
	int pid;
	int pathSize;
	char path[PACKAGESIZE];
	int puntero;
} t_pathMensaje;

typedef struct {
	int pid;
	int orden;	// 0=Iniciar, 1=Leer, 2=Escribir, 3=Finalizar
	int pagina;
} t_orden_memoria;

typedef struct {
	int pid;
	int paginas;
	int mensajeSize;
} t_respuesta_Swap;

int id1 = 5;
int RETARDO, socketMemoria;
t_log* logger;

int tamanioEstructura2(t_pathMensaje unaPersona);
int tamanioRespuesta(t_respuesta_Swap unaRespuesta);

void conectarHilos();
void recibirPath(int serverSocket);
char * obtenerLinea(char path[PACKAGESIZE], int puntero);
void enviarRespuesta(int socketPlanificador, t_respuesta_Swap respuestaMemoria);
void enviarAMemoria(int orden, int pagina, int pid);
void interpretarInstruccion(int serverSocket, t_pathMensaje unaPersona,
		char * linea);
void recibirRespuestaSwap(int serverSocket);

int main() {
	printf("\n");
	printf("----CPU----\n\n");

	logger = log_create("/home/utnso/github/tp-2015-2c-daft-punk-so/CPU/logsTP",
			"CPU", true, LOG_LEVEL_INFO);

	t_config* config;

	config = config_create(
			"/home/utnso/github/tp-2015-2c-daft-punk-so/CPU/config.cfg");

	tspaghetti spaghetti;

	spaghetti.ipPlanificador = config_get_string_value(config,
			"IP_PLANIFICADOR");
	spaghetti.puertoPlanificador = config_get_string_value(config,
			"PUERTO_PLANIFICADOR");
	spaghetti.ipMemoria = config_get_string_value(config, "IP_MEMORIA");
	spaghetti.puertoMemoria = config_get_string_value(config, "PUERTO_MEMORIA");
	spaghetti.numeroHilos = config_get_int_value(config, "CANTIDAD_HILOS");
	RETARDO = config_get_int_value(config, "RETARDO");

	int i = 1;
	t_list *listaDeHilos = list_create();
	while (i <= spaghetti.numeroHilos) {
		pthread_t unHilo;
		if(pthread_create(&unHilo, NULL, (void*) conectarHilos, &spaghetti)==0){
			list_add(listaDeHilos,&unHilo);
		}
		i++;
	}

	log_info(logger, "-------------------------------------------------");

	i=1;

	while (i <= spaghetti.numeroHilos) {
		int unHilo;
		unHilo = list_get(listaDeHilos,i);
		pthread_join(unHilo, NULL);
		i++;
	}

	list_clean(listaDeHilos);
	list_destroy(listaDeHilos);
	close(socketMemoria);
	log_destroy(logger);
	config_destroy(config);

	return 0;
}

void conectarHilos(void *context) {
	tspaghetti *spaghetti = context;

	int serverSocket;
	serverSocket = conectarse(spaghetti->ipPlanificador,
			spaghetti->puertoPlanificador);

	log_info(logger, "Conectado a planificador!\n\n");

	socketMemoria = conectarse(spaghetti->ipMemoria, spaghetti->puertoMemoria);

	log_info(logger, "Conectado a memoria!");

	t_idHilo mensaje;

	id1++;

	log_info(logger, "CPU %d creado", id1);

	mensaje.idNodo = id1;
	mensaje.cantHilos = spaghetti->numeroHilos;

	//Envio id y aviso la cantidad de conexiones
	void* package = malloc(sizeof(t_idHilo));

	memcpy(package, &mensaje.idNodo, sizeof(mensaje.idNodo));
	memcpy(package + sizeof(mensaje.idNodo), &mensaje.cantHilos,
			sizeof(mensaje.cantHilos));

	send(serverSocket, package, sizeof(t_idHilo), 0);

	free(package);

	recibirPath(serverSocket);

	close(serverSocket);

}

void recibirPath(int serverSocket) {
	int status = 1;

	t_pathMensaje unaPersona;

	void* package = malloc(tamanioEstructura2(unaPersona));

	while (status) {
		status = recv(serverSocket, (void*) package, sizeof(unaPersona.pid), 0);

		if (status == 0) {
			break;
		} else {
			memcpy(&unaPersona.pid, package, sizeof(unaPersona.pid));
			recv(serverSocket, (void*) (package + sizeof(unaPersona.pid)),
					sizeof(unaPersona.puntero), 0);
			memcpy(&unaPersona.puntero, package + sizeof(unaPersona.pid),
					sizeof(unaPersona.puntero));	//--
			recv(serverSocket,
					(void*) (package + sizeof(unaPersona.pid)
							+ sizeof(unaPersona.puntero)),
					sizeof(unaPersona.pathSize), 0);
			memcpy(&unaPersona.pathSize,
					package + sizeof(unaPersona.pid)
							+ sizeof(unaPersona.puntero),
					sizeof(unaPersona.pathSize));	//--

			void* package2 = malloc(unaPersona.pathSize);

			recv(serverSocket, (void*) package2, unaPersona.pathSize, 0);
			memcpy(&unaPersona.path, package2, unaPersona.pathSize);

			log_info(logger, "Contexto %s", unaPersona.path);

			char * linea = obtenerLinea(unaPersona.path, unaPersona.puntero);

			strncpy(unaPersona.path, " ", PACKAGESIZE);

			interpretarInstruccion(serverSocket, unaPersona, linea);

			sleep(RETARDO);

			free(linea);
			free(package2);
		}

	}

	free(package);
}

void recibirRespuestaSwap(int serverSocket) {
	t_respuesta_Swap respuesta;

	void* package = malloc(
			sizeof(respuesta.pid) + sizeof(respuesta.paginas)
					+ sizeof(respuesta.mensajeSize));

	recv(socketMemoria, (void*) package, sizeof(respuesta.pid), 0);
	memcpy(&respuesta.pid, package, sizeof(respuesta.pid));

	recv(socketMemoria, (void*) (package + sizeof(respuesta.pid)),
			sizeof(respuesta.paginas), 0);
	memcpy(&respuesta.paginas, package + sizeof(respuesta.pid),
			sizeof(respuesta.paginas));

	recv(socketMemoria,
			(void*) (package + sizeof(respuesta.pid) + sizeof(respuesta.paginas)),
			sizeof(respuesta.mensajeSize), 0);
	memcpy(&respuesta.mensajeSize,
			package + sizeof(respuesta.pid) + sizeof(respuesta.paginas),
			sizeof(respuesta.mensajeSize));

	log_info(logger, "Instruccion Ejecutada. mProc: %d. Parametros: %d",
			respuesta.pid, respuesta.paginas);

	enviarRespuesta(serverSocket, respuesta);

	free(package);

}

void enviarRespuesta(int socketPlanificador, t_respuesta_Swap respuestaMemoria) {
	void* respuestaPackage = malloc(tamanioRespuesta(respuestaMemoria));

	memcpy(respuestaPackage, &respuestaMemoria.pid,
			sizeof(respuestaMemoria.pid));
	memcpy(respuestaPackage + sizeof(respuestaMemoria.pid),
			&respuestaMemoria.paginas, sizeof(respuestaMemoria.paginas));
	memcpy(
			respuestaPackage + sizeof(respuestaMemoria.pid)
					+ sizeof(respuestaMemoria.paginas),
			&respuestaMemoria.mensajeSize,
			sizeof(respuestaMemoria.mensajeSize));

	send(socketPlanificador, respuestaPackage,
			tamanioRespuesta(respuestaMemoria), 0);
	log_info(logger, "Rafaga concluida. mProc: %d.", respuestaMemoria.pid);

	free(respuestaPackage);
}

char * obtenerLinea(char path[PACKAGESIZE], int puntero) {
	FILE* file;
	file = txt_open_for_read(path);
	char * linea = read_line(file, puntero - 1);

	txt_close_file(file);

	return linea;
}
void enviarAMemoria(int orden, int pagina, int pid) {
	t_orden_memoria orden_memoria;

	void* ordenPackage = malloc((sizeof(int) + sizeof(int) + sizeof(int)));

	orden_memoria.pid = pid;
	orden_memoria.orden = orden;
	orden_memoria.pagina = pagina;

	memcpy(ordenPackage, &orden_memoria.pid, sizeof(orden_memoria.pid));
	memcpy(ordenPackage + sizeof(pid), &orden_memoria.orden,
			sizeof(orden_memoria.orden));
	memcpy(ordenPackage + sizeof(pid) + sizeof(pagina), &orden_memoria.pagina,
			sizeof(orden_memoria.pagina));

	send(socketMemoria, ordenPackage, sizeof(int) + sizeof(int) + sizeof(int),
			0);

	free(ordenPackage);
}
void interpretarInstruccion(int serverSocket, t_pathMensaje unaPersona,
		char * linea) {
	char * pch = strtok(linea, " \n");
	int pagina;

	if (strncmp(pch, "iniciar", 7) == 0) {
		pch = strtok(NULL, " \n");
		pagina = strtol(pch, NULL, 10);
		enviarAMemoria(0, pagina, unaPersona.pid);
	} else {
		if (strncmp(pch, "leer", 4) == 0) {
			pch = strtok(NULL, " \n");
			pagina = strtol(pch, NULL, 10);
			enviarAMemoria(1, pagina, unaPersona.pid);
		} else {
			if (strncmp(pch, "finalizar", 9) == 0) {
				enviarAMemoria(3, 0, unaPersona.pid);
			}
		}
	}

	recibirRespuestaSwap(serverSocket);

}

int tamanioEstructura2(t_pathMensaje unaPersona) {
	return (sizeof(unaPersona.puntero) + sizeof(unaPersona.pathSize)
			+ sizeof(unaPersona.pid));
}
;

int tamanioRespuesta(t_respuesta_Swap unaRespuesta) {
	return (sizeof(unaRespuesta.pid) + sizeof(unaRespuesta.paginas)
			+ sizeof(unaRespuesta.mensajeSize));
}
;

