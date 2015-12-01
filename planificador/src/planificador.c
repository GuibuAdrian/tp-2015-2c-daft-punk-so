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
#include <errno.h>
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#include <commons/socket.h>
#include <commons/config.h>
#include <commons/collections/list.h>
#include <commons/log.h>

#define PACKAGESIZE 1024

typedef struct
{
    int idHilo;
    int cantHilos;
}t_mensaje1;

typedef struct
{
    int idHilo;
    int socketCliente;
    int disponible; // 1 = Disponible, 0 = NO Disponible
}t_hiloCPU;

typedef struct
{
    int pid;
    char * path;
    int puntero;
    int estado;  // 0 = Listo,  1 = Ejecutando,  2 = Bloqueado
    time_t tiempoEjecI;
    time_t tiempoEspeI;
    time_t tiempoRespI;
    double tiempoEspe;
    double tiempoResp;
}PCB;

typedef struct
{
    int pid;
}t_ready;

typedef struct
{
    int pid;
    int io;
}t_io;

typedef struct
{
	int pid;
    int puntero;
    int pathSize;
    char *path;
}t_pathMensaje;

typedef struct
{
	int pid;
	int mensajeSize; // 0 = INICIADO, 1 = FALLO, 2 = LEIDO, 3 = ESCRITO, 4 = FINALIZADO
	int paginas;
	int contentSize;
	char content[PACKAGESIZE];
}t_respuesta;

typedef struct
{
	int pcbReady;
	int idHilo;
    int socketCliente;
} t_enviarProceso;

t_log* logger;
t_list *listaCPUs, *listaPCB, *listaReady, *listaIO;
sem_t semPlani, semFZ, semCPU, semIO;
pthread_mutex_t mutexCPU, mutexPCB, mutexReady, mutexIO;
char *ALGORITMO_PLANIFICACION;
int pid=2, cantHilos, QUANTUM, socketCPUCarga;

int tamanioMensaje1(t_mensaje1 mensaje);
int tamanioHiloCPU(t_hiloCPU mensaje);
static t_hiloCPU *hiloCPU_create(int idNodo, int socket, int disponible);
static void hiloCPU_destroy(t_hiloCPU *self);
static PCB *PCB_create(int pid, char * path, int puntero, int estado, time_t tiempoEjecI, time_t tiempoEspeI, double tiempoEspe, time_t tiempoRespI, double tiempoResp);
static void PCB_destroy(PCB *self);
int tamanioPCB(PCB mensaje);
static t_ready *ready_create(int pid);
static void ready_destroy(t_ready *self);
static t_io *IO_create(int pid, int io);
static void IO_destroy(t_io *self);
int tamanioready(t_ready mensaje);
int tamanioEstructuraAEnviar(t_pathMensaje unaPersona);
int tamanioRespuesta(t_respuesta unaRespuesta);

t_hiloCPU* buscarCPU(int cpu);
t_hiloCPU* buscarCPUDisponible();
PCB* buscarReadyEnPCB(int pid);
PCB* buscarPCB(int pidF);
int encontrarPosicionEnReady(int pid);
int encontrarPosicionEnPCB(int pid);
int encontrarPosicionHiloCPU(int idHilo);

void correrPath(char * pch);
void PS();
void CPU();
void finalizarPID(int pidF);
void mostrarListos();
void mostrarBloqueados();
void cerrarConexiones();
void consola();
void recibirConexiones(char * PUERTO);
void ROUND_ROBIN();
void FIFO();
void planificador();
void entrada_salida();
int enviarPath(int socketCliente, int pid, char * path, int punteroProx);

int main()
{
	printf("\n");
	printf("~~~~~~~~~~PLANIFICADOR~~~~~~~~~~\n\n");


	logger = log_create("logsTP", "PLANIFICADOR", 0, LOG_LEVEL_INFO);

	t_config* config;

	config = config_create("config.cfg");

	char * PUERTO_ESCUCHA = config_get_string_value(config, "PUERTO_ESCUCHA");
	ALGORITMO_PLANIFICACION = config_get_string_value(config, "ALGORITMO_PLANIFICACION");
	QUANTUM = config_get_int_value(config, "QUANTUM");


	listaCPUs = list_create();
	listaPCB = list_create();
	listaReady = list_create();
	listaIO = list_create();


	recibirConexiones(PUERTO_ESCUCHA);


	pthread_mutex_init(&mutexCPU, NULL);
	pthread_mutex_init(&mutexPCB, NULL);
	pthread_mutex_init(&mutexReady, NULL);
	pthread_mutex_init(&mutexIO, NULL);
	sem_init(&semPlani, 0, 0);
	sem_init(&semFZ, 0, 1);
	sem_init(&semCPU, 0, cantHilos);
	sem_init(&semIO, 0, 0);


	pthread_t hiloPlanificador, hiloEntrada_salida;
	pthread_create(&hiloPlanificador,NULL,(void*) planificador, NULL);
	pthread_create(&hiloEntrada_salida,NULL,(void*) entrada_salida, NULL);

	consola();

	pthread_join(hiloPlanificador, NULL);
	pthread_join(hiloEntrada_salida, NULL);


	close(socketCPUCarga);

	list_destroy_and_destroy_elements(listaPCB,(void*) PCB_destroy);
	list_destroy(listaCPUs);

	config_destroy(config);
	log_info(logger, "---------------------FIN---------------------");
	log_destroy(logger);

	return 0;
}

int cargaListaCPU(int socketCliente)
{
	log_info(logger, "Recibiendo CPU");

	int cantHilos;

	t_mensaje1 mensaje;

	void* package=malloc(tamanioMensaje1(mensaje));

	recv(socketCliente,package,sizeof(int),0);
	memcpy(&mensaje.idHilo,package,sizeof(int));
	recv(socketCliente,package+sizeof(int),sizeof(int),0);
	memcpy(&cantHilos,package+sizeof(int),sizeof(int));

	log_info(logger, "CPU: %d Conectado", mensaje.idHilo);

	list_add(listaCPUs, hiloCPU_create(mensaje.idHilo,socketCliente, 1));	//Cargo la lista con los sockets CPU

	free(package);

	return cantHilos;
};
void recibirConexiones(char * PUERTO)
{

	int i=0;
	int socketCliente, listenningSocket, result, maxfd;

	listenningSocket = recibirLlamada(PUERTO);
	socketCPUCarga= aceptarLlamada(listenningSocket);

	fd_set readset;


	FD_ZERO(&readset);
	FD_SET(listenningSocket, &readset);

	maxfd = listenningSocket;

	do
	{
		result = select(maxfd + 1, &readset, NULL, NULL, NULL);

		if (result < 0)
		{
			log_error(logger,"Error in select(): %s", strerror(errno));
		}
		else if (result > 0)
		{
			if (FD_ISSET(listenningSocket, &readset))
			{
				socketCliente = aceptarLlamada(listenningSocket);

				if (socketCliente < 0)
				{
					log_error(logger, "Error in accept(): %s", strerror(errno));
				}

				cantHilos = cargaListaCPU(socketCliente); //Identifico el Nodo

				i++;

			}
		} //Fin else if  (result > 0)
	} while (i!=cantHilos);


	close(listenningSocket);
}

void recibirCjtoRespuestas(int pid1, int socketCliente)
{
	t_respuesta respuesta;

	void* package = malloc(tamanioRespuesta(respuesta));

	int cpu=1;


	while(respuesta.pid !=-1)
	{
		recv(socketCliente,(void*)package, sizeof(respuesta.pid), 0);
		memcpy(&respuesta.pid,package,sizeof(respuesta.pid));
		recv(socketCliente,(void*) (package+sizeof(respuesta.pid)), sizeof(respuesta.paginas), 0);
		memcpy(&respuesta.mensajeSize, package+sizeof(respuesta.pid),sizeof(respuesta.mensajeSize));
		recv(socketCliente,(void*) (package+sizeof(respuesta.pid)+sizeof(respuesta.mensajeSize)), sizeof(respuesta.paginas), 0);
		memcpy(&respuesta.paginas, package+sizeof(respuesta.pid)+sizeof(respuesta.mensajeSize),sizeof(respuesta.paginas));
		recv(socketCliente,(void*) (package+sizeof(respuesta.pid)+sizeof(respuesta.mensajeSize)+sizeof(respuesta.paginas)), sizeof(respuesta.contentSize), 0);
		memcpy(&respuesta.contentSize, package+sizeof(respuesta.pid)+sizeof(respuesta.mensajeSize)+sizeof(respuesta.paginas), sizeof(respuesta.contentSize));

		if(respuesta.pid==-1)
		{
			cpu = -1;
		}
		else
		{
			pid1 = respuesta.pid;
		}

		void* package2=malloc(respuesta.contentSize);

		recv(socketCliente,(void*) package2, respuesta.contentSize, 0);//campo longitud(NO SIZEOF DE LONGITUD)
		memcpy(&respuesta.content, package2, respuesta.contentSize);

		if(cpu!=-1)
		{
			if( (respuesta.mensajeSize)==0 )
			{
				log_info(logger, "mProc %d - Iniciado", respuesta.pid);
			}
			else
			{
				if( (respuesta.mensajeSize)==2 )
				{
					log_info(logger, "mProc %d - Pagina %d leida: %s", respuesta.pid, respuesta.paginas, respuesta.content);
				}
				else
				{
					log_info(logger, "mProc %d - Pagina %d escrita: %s", respuesta.pid, respuesta.paginas, respuesta.content);
				}

			}

		}

		free(package2);
	}

	pthread_mutex_lock(&mutexPCB);

	printf("Recibiendo mProc:%d.\n", pid1);

	PCB *pcb = buscarReadyEnPCB(pid1);
	int posPCB =  encontrarPosicionEnPCB(pcb->pid);	//Encontrar pos en listaPCB
	time_t tiempoAhora;
	double resp = pcb->tiempoResp;
	time(&tiempoAhora);

	resp = resp + difftime(tiempoAhora, pcb->tiempoRespI);

	list_replace_and_destroy_element(listaPCB, posPCB, PCB_create(pcb->pid, pcb->path, pcb->puntero+1, 2,
			pcb->tiempoEjecI, pcb->tiempoEspeI, pcb->tiempoEspe, pcb->tiempoRespI, resp), (void*)PCB_destroy);//Pongo al mProc en bloqueado
	pthread_mutex_unlock(&mutexPCB);

	free(package);
}

int recibirRespuesta(int socketCliente)
{
	t_respuesta respuesta;
	PCB *pcb;

	void* package = malloc(tamanioRespuesta(respuesta));

	recv(socketCliente,(void*)package, sizeof(respuesta.pid), 0);
	memcpy(&respuesta.pid,package,sizeof(respuesta.pid));
	recv(socketCliente,(void*) (package+sizeof(respuesta.pid)), sizeof(respuesta.paginas), 0);
	memcpy(&respuesta.mensajeSize, package+sizeof(respuesta.pid),sizeof(respuesta.mensajeSize));
	recv(socketCliente,(void*) (package+sizeof(respuesta.pid)+sizeof(respuesta.mensajeSize)), sizeof(respuesta.paginas), 0);
	memcpy(&respuesta.paginas, package+sizeof(respuesta.pid)+sizeof(respuesta.mensajeSize),sizeof(respuesta.paginas));
	recv(socketCliente,(void*) (package+sizeof(respuesta.pid)+sizeof(respuesta.mensajeSize)+sizeof(respuesta.paginas)), sizeof(respuesta.contentSize), 0);
	memcpy(&respuesta.contentSize, package+sizeof(respuesta.pid)+sizeof(respuesta.mensajeSize)+sizeof(respuesta.paginas), sizeof(respuesta.contentSize));


	void* package2=malloc(respuesta.contentSize);

	recv(socketCliente,(void*) package2, respuesta.contentSize, 0);//campo longitud(NO SIZEOF DE LONGITUD)
	memcpy(&respuesta.content, package2, respuesta.contentSize);


	if( (respuesta.mensajeSize)==0 )
	{
		pthread_mutex_lock(&mutexPCB);

		pcb = buscarReadyEnPCB(respuesta.pid);
		int posPCB =  encontrarPosicionEnPCB(pcb->pid);	//Encontrar pos en listaPCB
		time_t ejecI;

		time(&ejecI);

		list_replace_and_destroy_element(listaPCB, posPCB, PCB_create(pcb->pid, pcb->path, pcb->puntero, pcb->estado,
				ejecI, pcb->tiempoEspeI, pcb->tiempoEspe, pcb->tiempoRespI, pcb->tiempoResp), (void*)PCB_destroy);//Pongo al mProc en bloqueado


		pthread_mutex_unlock(&mutexPCB);
	}
	else
	{
		if( (respuesta.mensajeSize)==1 )
		{
			log_info(logger, "mProc %d - Fallo", respuesta.pid);

			sem_post(&semFZ);
			finalizarPID(respuesta.pid);
			sem_wait(&semFZ);
		}
		else
		{
			if( (respuesta.mensajeSize)==2 )
			{

			}
			else
			{
				if( (respuesta.mensajeSize)==3 )
				{
					recibirCjtoRespuestas(respuesta.pid, socketCliente);

					log_info(logger, "mProc %d finalizado", respuesta.pid);

					pthread_mutex_lock(&mutexPCB);
					pcb = buscarReadyEnPCB(respuesta.pid);
					int posPCB =  encontrarPosicionEnPCB(pcb->pid);	//Encontrar pos en listaPCB

					time_t ejecF, ejecI;

					ejecI = pcb->tiempoEjecI;

					time(&ejecF);
					struct tm * timeinfo;

					timeinfo = localtime( &ejecI );

					printf("\nInicio mProc:%d %d:%d\n", respuesta.pid, timeinfo->tm_min, timeinfo->tm_sec);

					timeinfo = localtime( &ejecF );

					printf("\nFIN mProc:%d %d:%d\n", respuesta.pid, timeinfo->tm_min, timeinfo->tm_sec);

					double tiempoEjec = difftime(ejecF, pcb->tiempoEjecI);

					printf("\nmProc:%d. Ejec :%.2f. Espera: %.2f. Resp: %.2f\n", respuesta.pid, tiempoEjec, pcb->tiempoEspe, pcb->tiempoResp);

					list_remove_and_destroy_element(listaPCB, posPCB, (void*) PCB_destroy);
					pthread_mutex_unlock(&mutexPCB);


					return -1;
				}
				else
				{
					if( (respuesta.mensajeSize)==4 )
					{

					}
					else
					{
						int IO = respuesta.paginas;

						recibirCjtoRespuestas(respuesta.pid, socketCliente);

						log_info(logger, "mProc %d en entrada-salida de tiempo %d", respuesta.pid, IO);

						pthread_mutex_lock(&mutexPCB);

						pcb = buscarReadyEnPCB(respuesta.pid);
						int posPCB =  encontrarPosicionEnPCB(pcb->pid);	//Encontrar pos en listaPCB

						//Pongo al mProc en bloqueado
						list_replace_and_destroy_element(listaPCB, posPCB, PCB_create(pcb->pid, pcb->path, pcb->puntero, 2,
								pcb->tiempoEjecI, pcb->tiempoEspeI, pcb->tiempoEspe, pcb->tiempoRespI, pcb->tiempoResp), (void*)PCB_destroy);
						pthread_mutex_unlock(&mutexPCB);


						pthread_mutex_lock(&mutexIO);
						list_add(listaIO, IO_create(respuesta.pid, IO)); //Agrego el proceso NUEVO a Ready

						pthread_mutex_unlock(&mutexIO);;

						sem_post(&semIO);

						free(package);
						free(package2);

						return IO;
					}
				}
			}
		}
	}

	free(package);
	free(package2);

	return 0;
}
/*
void ROUND_ROBIN(void* args)
{
	time_t tiempoAhora;
	t_enviarProceso *mensaje;
	mensaje = (t_enviarProceso*) args;

	int pidReady = mensaje->pcbReady;

	PCB* pcbReady = buscarReadyEnPCB(pidReady);	//Busco al ready en el PCB

	int idHiloCPU = mensaje->idHilo;
	int socketCliente = mensaje->socketCliente;
	int IO;
	int puntero = pcbReady->puntero;
	int i = pcbReady->puntero;
	int totalLineas = pcbReady->totalLineas;
	char* path = strdup(pcbReady->path);
	time_t ejecI = pcbReady->tiempoEjecI;
	time_t espeI = pcbReady->tiempoEspeI;
	time_t respI = pcbReady->tiempoRespI;
	double espe = pcbReady->tiempoEspe;
	double resp = pcbReady->tiempoResp;

	pthread_mutex_lock(&mutexPCB);
	log_info(logger, "Correr %s, mProc: %d, en %d", path, pidReady, idHiloCPU);

	time(&tiempoAhora);

	printf("Correr %s, mProc: %d, en %d\n", path, pidReady, idHiloCPU);

	espe = espe + difftime(tiempoAhora, espeI);

	int posPCB =  encontrarPosicionEnPCB(pidReady);	//Encontrar pos en listaPCB
	list_replace_and_destroy_element(listaPCB, posPCB, PCB_create(pidReady, path, puntero, 1, totalLineas, ejecI, espeI ,espe, time(&respI), resp), (void*)PCB_destroy);

	pthread_mutex_unlock(&mutexPCB);

	int Q = 0;

	while( ( (i-1)<=(totalLineas) ) && ( Q<QUANTUM ) )
	{
		pcbReady = buscarReadyEnPCB(pidReady);

		IO = enviarPath(socketCliente, pidReady, pcbReady->path, pcbReady->puntero);

		if(IO>0)
		{
			break;
		}

		pcbReady = buscarReadyEnPCB(pidReady);
		if(pcbReady == NULL)
		{
			break;
		}

		puntero = pcbReady->puntero;


		sem_wait(&semFZ);
		i=puntero+1;

		pthread_mutex_lock(&mutexPCB);
		posPCB =  encontrarPosicionEnPCB(pidReady);	//Encontrar pos en listaPCB

		PCB* pcbAux = list_replace(listaPCB, posPCB, PCB_create(pidReady, path, (puntero+1), 1, totalLineas,
				pcbReady->tiempoEjecI, pcbReady->tiempoEspeI, pcbReady->tiempoEspe, pcbReady->tiempoRespI, pcbReady->tiempoResp));

		PCB_destroy(pcbAux);


		pthread_mutex_unlock(&mutexPCB);

		sem_post(&semFZ);

		Q++;
	}

	if( (i-1)>(totalLineas) )
	{
		pthread_mutex_lock(&mutexPCB);
		posPCB =  encontrarPosicionEnPCB(pidReady);
		list_remove_and_destroy_element(listaPCB, posPCB, (void*) PCB_destroy);
		pthread_mutex_unlock(&mutexPCB);
	}
	if(Q>=QUANTUM)
	{
		log_info(logger,"FIN Q");

		int message = 2;
		send(socketCPUCarga, &message, sizeof(int), 0);
		send(socketCPUCarga, &pidReady, sizeof(int), 0);

		recibirCjtoRespuestas(pidReady, socketCPUCarga);

		list_add(listaReady, ready_create(pidReady)); //Pongo el mProc en ready

		pthread_mutex_lock(&mutexPCB);

		pcbReady = buscarReadyEnPCB(pidReady);
		posPCB =  encontrarPosicionEnPCB(pidReady);	//Encontrar pos en listaPCB

		list_replace_and_destroy_element(listaPCB, posPCB, PCB_create(pidReady, path, (puntero+1), 0, totalLineas,
				pcbReady->tiempoEjecI, time(&pcbReady->tiempoEspeI), pcbReady->tiempoEspe, pcbReady->tiempoRespI, pcbReady->tiempoResp), (void*)PCB_destroy);

		pthread_mutex_unlock(&mutexPCB);

		sem_post(&semPlani);
	}

	pthread_mutex_lock(&mutexCPU);
	int posCPU = encontrarPosicionHiloCPU(idHiloCPU); //Busco posicion del CPU
	list_replace_and_destroy_element(listaCPUs, posCPU, hiloCPU_create(idHiloCPU, socketCliente, 1), (void*) hiloCPU_destroy);	//Pongo en Disponible al CPU q usaba
	pthread_mutex_unlock(&mutexCPU);
	sem_post(&semCPU);


	free(path);
	free(args);
}
*/
void FIFO(void *args)
{
	time_t tiempoAhora;
	t_enviarProceso *mensaje;
	mensaje = (t_enviarProceso*) args;

	int pidReady = mensaje->pcbReady;

	PCB* pcbReady = buscarReadyEnPCB(pidReady);	//Busco al ready en el PCB

	int idHiloCPU = mensaje->idHilo;
	int socketCliente = mensaje->socketCliente;
	int IO;
	int puntero = pcbReady->puntero;
	char* path = strdup(pcbReady->path);
	time_t ejecI = pcbReady->tiempoEjecI;
	time_t espeI = pcbReady->tiempoEspeI;
	time_t respI = pcbReady->tiempoRespI;
	double espe = pcbReady->tiempoEspe;
	double resp = pcbReady->tiempoResp;

	pthread_mutex_lock(&mutexPCB);
	log_info(logger, "Correr %s, mProc: %d, en %d", path, pidReady, idHiloCPU);

	time(&tiempoAhora);

	printf("Correr %s, mProc: %d, en %d\n", path, pidReady, idHiloCPU);

	espe = espe + difftime(tiempoAhora, espeI);

	int posPCB =  encontrarPosicionEnPCB(pidReady);	//Encontrar pos en listaPCB
	list_replace_and_destroy_element(listaPCB, posPCB, PCB_create(pidReady, path, puntero, 1, ejecI, espeI ,espe, time(&respI), resp), (void*)PCB_destroy);

	pthread_mutex_unlock(&mutexPCB);

	while( IO==0 )
	{
		pcbReady = buscarReadyEnPCB(pidReady);

		IO = enviarPath(socketCliente, pidReady, path, pcbReady->puntero);

		if(IO>0)
		{
			break;
		}

		pcbReady = buscarReadyEnPCB(pidReady);

		if(pcbReady == NULL)
		{
			break;
		}

		puntero = pcbReady->puntero+1;

		sem_wait(&semFZ);

		pthread_mutex_lock(&mutexPCB);
		posPCB =  encontrarPosicionEnPCB(pidReady);	//Encontrar pos en listaPCB
		list_replace_and_destroy_element(listaPCB, posPCB, PCB_create(pidReady, path, puntero, 1,
				pcbReady->tiempoEjecI, pcbReady->tiempoEspeI, pcbReady->tiempoEspe, pcbReady->tiempoRespI, pcbReady->tiempoResp), (void*)PCB_destroy);
		pthread_mutex_unlock(&mutexPCB);

		sem_post(&semFZ);
	}

	pthread_mutex_lock(&mutexCPU);
	int posCPU = encontrarPosicionHiloCPU(idHiloCPU); //Busco posicion del CPU

	//Pongo en Disponible al CPU q usaba
	list_replace_and_destroy_element(listaCPUs, posCPU, hiloCPU_create(idHiloCPU, socketCliente, 1), (void*) hiloCPU_destroy);
	pthread_mutex_unlock(&mutexCPU);
	sem_post(&semCPU);

	free(path);
	free(args);
}

void planificador()
{
	while(1)
	{
		sem_wait(&semPlani);
		if(list_size(listaCPUs) == 0)
		{
			break;
		}

		sem_wait(&semCPU);

		pthread_mutex_lock(&mutexCPU);
		t_hiloCPU* hiloCPU = buscarCPUDisponible(); //Busco algun CPU que este disponible
		int posCPU = encontrarPosicionHiloCPU(hiloCPU->idHilo); //Busco posicion del CPU disponible

		int socketCliente = hiloCPU->socketCliente;
		int idHiloCPU = hiloCPU->idHilo;

		//Pongo al CPU en ocupado (0)
		list_replace_and_destroy_element(listaCPUs, posCPU, hiloCPU_create(idHiloCPU, socketCliente, 0), (void*) hiloCPU_destroy);
		pthread_mutex_unlock(&mutexCPU);

		pthread_mutex_lock(&mutexReady);
		t_ready *unReady = list_remove(listaReady, 0);	//Busco al primer ready
		int pidReady = unReady->pid;
		ready_destroy(unReady);
		pthread_mutex_unlock(&mutexReady);

		pthread_t hilo;

		t_enviarProceso *unaPersona;	//Creo la estructura a enviar

		unaPersona = (t_enviarProceso *)malloc(sizeof(t_enviarProceso));
		unaPersona->pcbReady = pidReady;
		unaPersona->idHilo = idHiloCPU;
		unaPersona->socketCliente = socketCliente;


		if (strncmp(ALGORITMO_PLANIFICACION,"FIFO", 4) == 0)
		{
			pthread_create(&hilo, NULL, (void*) FIFO, (void*) unaPersona);
		}
		else
		{
			//pthread_create(&hilo, NULL, (void*) ROUND_ROBIN, (void*) unaPersona);
		}

	}
}

void entrada_salida()
{
	while(1)
	{
		sem_wait(&semIO);

		if(list_size(listaCPUs) == 0)
		{
			break;
		}

		t_io *unIO = list_remove(listaIO, 0);	//Busco al primer io

		sleep(unIO->io);

		list_add(listaReady, ready_create(unIO->pid)); //Agrego el proceso NUEVO a Ready

		pthread_mutex_lock(&mutexPCB);
		PCB* pcbReady = buscarReadyEnPCB(unIO->pid);

		time_t espeI;
		time(&espeI);

		int posPCB =  encontrarPosicionEnPCB(unIO->pid);	//Encontrar pos en listaPCB
		list_replace_and_destroy_element(listaPCB, posPCB, PCB_create(pcbReady->pid, pcbReady->path, pcbReady->puntero, 0,
				pcbReady->tiempoEjecI, espeI, pcbReady->tiempoEspe, pcbReady->tiempoRespI, pcbReady->tiempoResp), (void*)PCB_destroy);

		pthread_mutex_unlock(&mutexPCB);

		sem_post(&semPlani);


		IO_destroy(unIO);
	}

}

int enviarPath(int socketCliente, int pid1, char * path, int punteroProx)
{
	t_pathMensaje unaPersona;
	unaPersona.pid = pid1;
	unaPersona.pathSize=strlen(path);
	unaPersona.path = strdup(path);
	unaPersona.puntero = punteroProx;

	void* package = malloc(tamanioEstructuraAEnviar(unaPersona));

	memcpy(package,&unaPersona.pid,sizeof(unaPersona.pid));
	memcpy(package+sizeof(unaPersona.pid),&unaPersona.puntero,sizeof(unaPersona.puntero));
	memcpy(package+sizeof(unaPersona.pid)+sizeof(unaPersona.puntero), &unaPersona.pathSize, sizeof(unaPersona.pathSize));
	memcpy(package+sizeof(unaPersona.pid)+sizeof(unaPersona.puntero)+sizeof(unaPersona.pathSize), unaPersona.path, unaPersona.pathSize);

	send(socketCliente,package, tamanioEstructuraAEnviar(unaPersona),0);

	int IO = recibirRespuesta(socketCliente);

	free(unaPersona.path);

	free(package);

	return IO;
}

void correrPath(char * pch)
{
	pid++;

	time_t espeI;

	list_add(listaReady, ready_create(pid)); //Agrego el proceso NUEVO a Ready

	list_add(listaPCB, PCB_create(pid, pch, 2, 0, 0, time(&espeI), 0, 0, 0));	//Agrego el proceso NUEVO al PCB

	sem_post(&semPlani);


	printf("\n");
}
void PS()
{
	PCB *new;

	int i;

	for (i = 0; i < list_size(listaPCB); i++)
	{
		new = list_get(listaPCB, i);
		printf("mProc %d: %s -> ", new->pid, new->path);

		if(new->estado==0)
		{
			printf("Listo\n");
		}
		else if(new->estado==1)
		{
			printf("Ejecutando\n");
		}
		else
		{
			printf("Bloqueado\n");
		}
	};
	printf("\n");
}
void CPU()
{
	t_hiloCPU* new2;
	t_mensaje1 mensaje;

	int i, cpu = 0;

	for(i=0;i<list_size(listaCPUs);i++)
	{
		new2 = list_get(listaCPUs,i);

		printf("Disponible %d\n",new2->disponible);
		printf("Socket %d\n",new2->socketCliente);
		printf("PID %d\n",new2->idHilo);
	}
	int message = 1;
	send(socketCPUCarga, &message, sizeof(int), 0);

	void* package=malloc(tamanioMensaje1(mensaje));

	while(cpu!=-1)
	{
		recv(socketCPUCarga,package,sizeof(int),0);
		memcpy(&mensaje.idHilo,package,sizeof(int));
		recv(socketCPUCarga,package+sizeof(int),sizeof(int),0);
		memcpy(&mensaje.cantHilos,package+sizeof(int),sizeof(int));

		if(mensaje.idHilo==-1)
		{
			cpu=-1;
		}
		else
		{
			printf("CPU: %d, Carga: %d\n", mensaje.idHilo, mensaje.cantHilos);
		}
	}

	free(package);
}
void finalizarPID(int pidF)
{
	int posPCB =  encontrarPosicionEnPCB(pidF);	//Encontrar pos en listaPCB
	PCB* unPCB = buscarPCB(pidF);

	sem_post(&semFZ);
	list_replace_and_destroy_element(listaPCB, posPCB, PCB_create(unPCB->pid,unPCB->path, -2, 1,
			unPCB->tiempoEjecI, unPCB->tiempoEspeI, unPCB->tiempoEspe, unPCB->tiempoRespI, unPCB->tiempoResp), (void*) PCB_destroy);
	sem_wait(&semFZ);

}
void mostrarListos()
{
	t_ready *new;

	int i;

	printf("----Ready----\n");
	for(i=0;i<list_size(listaReady);i++)
	{
		new = list_get(listaReady, i);
		printf("PID: %d\n", new->pid);
	}
}
void mostrarBloqueados()
{
	t_io *new;

	int i;
	printf("\n----IO----\n");
	for(i=0;i<list_size(listaIO);i++)
	{
		new = list_get(listaIO, i);
		printf("PID: %d\n", new->pid);
	}
}
void cerrarConexiones()
{
	int i;
	t_hiloCPU *unHilo;

	for (i = 0; i < list_size(listaCPUs); i++)
	{
		unHilo = list_get(listaCPUs, i);

		close(unHilo->socketCliente);
	};
}

void consola()
{
	char comando[PACKAGESIZE];

	while(1)
	{
		printf("Ingresar comando: ");
		fgets(comando, PACKAGESIZE, stdin);
		printf("\n");

		char * pch;

		pch = strtok(comando," \n");

		if (strncmp(pch,"cr", 2) == 0) //Correr PATH
		{
			pch = strtok(NULL," \n");

			correrPath(pch);

			continue;
		}
		else
		{
			if (strncmp(pch, "fz", 2) == 0) //Finalizar PID
			{
				pch = strtok(NULL," \n");
				int ret = strtol(pch, NULL, 10);

				finalizarPID(ret);

				continue;
			}
		else
		{
			if (strncmp(pch, "ps", 2) == 0) //Correr PS
			{
				PS();

				continue;
			}
		else
		{
			if (strncmp(pch, "cpu", 3) == 0)
			{
				CPU();

				continue;
			}
		else
		{
			if (strncmp(pch, "rd", 3) == 0)
			{
				mostrarListos();

				continue;
			}
		else
		{
			if (strncmp(pch, "bl", 3) == 0)
			{
				mostrarBloqueados();

				continue;
			}
		else
		{
			if (strncmp(pch, "man", 3) == 0)
			{
				printf("--------------COMANDOS-------------- \n");
				printf("cr: CorreR PATH \n");
				printf("fz: FinaliZar PID \n");
				printf("ps: PS \n");
				printf("cpu: CPU \n");
				printf("rd: Mostrar Ready \n");
				printf("bl: Mostrar Bloqueados \n");
				printf("fin: Finaliza la consola \n");
				printf("\n");

				continue;
			}
		else
		{
			if (strncmp(pch, "fin", 3) ==0)
			{
				printf("Chau! (al estilo Nivel X)\n");

				cerrarConexiones();

				list_clean_and_destroy_elements(listaCPUs, (void*) hiloCPU_destroy);

				sem_post(&semPlani);
				sem_post(&semIO);
				break;
			}
		else
		{
			printf("Error Comando \n");

			continue;
		}//Fin error comando
		}//Fin fin
		}//Fin man
		}//Fin bl
		}//Fin rd
		}//Fin cpu
		}//Fin ps
		}//Fin fz


		free(pch);

    }//Fin while
}//Fin main

int tamanioMensaje1(t_mensaje1 mensaje)
{
	return sizeof(mensaje.idHilo)+sizeof(mensaje.cantHilos);
}
int tamanioHiloCPU(t_hiloCPU mensaje)
{
    return sizeof(mensaje.idHilo)+sizeof(mensaje.socketCliente)+sizeof(mensaje.disponible);

};
static t_hiloCPU *hiloCPU_create(int idNodo, int socket, int disponible)
{
    t_hiloCPU *new = malloc(sizeof(t_hiloCPU));
    new->idHilo = idNodo;
    new->socketCliente = socket;
    new->disponible = disponible;

    return new;
}
static void hiloCPU_destroy(t_hiloCPU *self)
{
    free(self);
}
static PCB *PCB_create(int pid, char * path, int puntero, int estado, time_t tiempoEjecI, time_t tiempoEspeI, double tiempoEspe, time_t tiempoRespI, double tiempoResp)
{
	 PCB *new = malloc(sizeof(PCB));
	 new->path = strdup(path);
	 new->pid = pid;
	 new->puntero = puntero;
	 new->estado = estado;
	 new->tiempoEjecI = tiempoEjecI;
	 new->tiempoEspeI = tiempoEspeI;
	 new->tiempoEspe = tiempoEspe;
	 new->tiempoRespI = tiempoRespI;
	 new->tiempoResp = tiempoResp;

	 return new;
}
static void PCB_destroy(PCB *self)
{
	free(self->path);
    free(self);
}
int tamanioPCB(PCB mensaje)
{
    return sizeof(mensaje.estado)+sizeof(mensaje.pid)+sizeof(mensaje.puntero)+PACKAGESIZE;
};
static t_ready *ready_create(int pid)
{
	t_ready *new = malloc(sizeof(t_ready));
	new->pid = pid;

	return new;
}
static void ready_destroy(t_ready *self)
{
    free(self);
}
static t_io *IO_create(int pid, int io)
{
	t_io *new = malloc(sizeof(t_io));
	new->pid = pid;
	new->io = io;

	return new;
}
static void IO_destroy(t_io *self)
{
    free(self);
}
int tamanioready(t_ready mensaje)
{
    return sizeof(mensaje.pid);
};
int tamanioEstructuraAEnviar(t_pathMensaje unaPersona)
{
	return (sizeof(unaPersona.pid)+sizeof(unaPersona.puntero)+sizeof(unaPersona.pathSize)+strlen(unaPersona.path));
};
int tamanioRespuesta(t_respuesta unaRespuesta)
{
	return (sizeof(unaRespuesta.pid)+sizeof(unaRespuesta.paginas)+sizeof(unaRespuesta.mensajeSize)+sizeof(unaRespuesta.contentSize));
};

t_hiloCPU* buscarCPU(int cpu)
{
	bool compararPorIdentificador(t_hiloCPU *unaCaja)
	{
		if (unaCaja->idHilo == cpu)
		{
			return 1;
		}

		return 0;
	}
	return (list_find(listaCPUs, (void*) compararPorIdentificador));
}
t_hiloCPU* buscarCPUDisponible()
{
	bool compararPorIdentificador(t_hiloCPU *unaCaja)
	{
		if (unaCaja->disponible == 1)
		{
			return 1;
		}

		return 0;
	}
	return (list_find(listaCPUs, (void*) compararPorIdentificador));
}
PCB* buscarReadyEnPCB(int pid)
{
	bool compararPorIdentificador2(PCB *unaCaja)
	{
		if (unaCaja->pid == pid)
		{
			return 1;
		}

		return 0;
	}
	return list_find(listaPCB, (void*) compararPorIdentificador2);
}
PCB* buscarPCB(int pidF)
{
	bool compararPorIdentificador2(PCB *unaCaja)
	{
		if (unaCaja->pid == pidF)
		{
			return 1;
		}

		return 0;
	}
	return list_find(listaPCB, (void*) compararPorIdentificador2);
}

int encontrarPosicionHiloCPU(int idHilo)
{
	t_hiloCPU* new;

	int i=0;
	int encontrado = 1;

	while( (i<list_size(listaCPUs)) && encontrado!=0)
	{
		new = list_get(listaCPUs,i);

		if(new->idHilo == idHilo)
		{
			encontrado = 0;
		}
		else
		{
			i++;
		}
	}
	return i;
}
int encontrarPosicionEnReady(int pid)
{
	t_ready* new;

	int i=0;
	int encontrado = 1;

	while( (i<list_size(listaReady)) && encontrado!=0)
	{
		new = list_get(listaReady,i);

		if(new->pid == pid)
		{
			encontrado = 0;
		}
		else
		{
			i++;
		}
	}
	return i;
}
int encontrarPosicionEnPCB(int pid)
{
	PCB* new;

	int i=0;
	int encontrado = 1;

	while( (i<list_size(listaPCB)) && encontrado!=0)
	{
		new = list_get(listaPCB,i);

		if(new->pid == pid)
		{
			encontrado = 0;
		}
		else
		{
			i++;
		}
	}
	return i;
}
