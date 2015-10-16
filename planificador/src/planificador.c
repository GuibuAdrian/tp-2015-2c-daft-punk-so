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
#include <pthread.h>
#include <semaphore.h>

#include <commons/socket.h>
#include <commons/config.h>
#include <commons/collections/list.h>
#include <commons/txt.h>
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
}PCB;

typedef struct
{
    int pid;
}t_ready;

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
	FILE* file;
	PCB* pcbReady;
	int posPCB;
} t_enviarProceso;

t_log* logger;
t_list *listaCPUs, *listaPCB, *listaReady;
sem_t semPlani,semFZ, semCPU;
pthread_mutex_t mutex, mutex2, mutex3, mutex4;
char *ALGORITMO_PLANIFICACION;
int pid=2, totalLineas, cantHilos, QUANTUM;

int tamanioMensaje1(t_mensaje1 mensaje);
int tamanioHiloCPU(t_hiloCPU mensaje);
static t_hiloCPU *hiloCPU_create(int idNodo, int socket, int disponible);
static void hiloCPU_destroy(t_hiloCPU *self);
static PCB *PCB_create(int pid, char * path, int puntero, int estado);
static void PCB_destroy(PCB *self);
int tamanioPCB(PCB mensaje);
static t_ready *ready_create(int pid);
static void ready_destroy(t_ready *self);
int tamanioready(t_ready mensaje);
int tamanioEstructuraAEnviar(t_pathMensaje unaPersona);
int tamanioRespuesta(t_respuesta unaRespuesta);

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
void cerrarConexiones();
void consola();
void recibirConexiones(char * PUERTO);
void ROUND_ROBIN();
void FIFO();
void planificador();
void enviarPath(int socketCliente, int pid, char * path, int punteroProx);

int main()
{
	printf("\n");
	printf("~~~~~~~~~~PLANIFICADOR~~~~~~~~~~\n\n");


	logger = log_create("/home/utnso/github/tp-2015-2c-daft-punk-so/planificador/logsTP", "PLANIFICADOR", true, LOG_LEVEL_INFO);

	t_config* config;

	config = config_create("/home/utnso/github/tp-2015-2c-daft-punk-so/planificador/config.cfg");

	char * PUERTO_ESCUCHA = config_get_string_value(config, "PUERTO_ESCUCHA");
	ALGORITMO_PLANIFICACION = config_get_string_value(config, "ALGORITMO_PLANIFICACION");
	QUANTUM = config_get_int_value(config, "QUANTUM");


	listaCPUs = list_create();
	listaPCB = list_create();
	listaReady = list_create();


	recibirConexiones(PUERTO_ESCUCHA);


	pthread_mutex_init(&mutex, NULL);
	pthread_mutex_init(&mutex2, NULL);
	pthread_mutex_init(&mutex3, NULL);
	pthread_mutex_init(&mutex4, NULL);
	sem_init(&semPlani, 0, 0);
	sem_init(&semFZ, 0, 1);   //Semaforo para comando FZ
	sem_init(&semCPU, 0, cantHilos);


	pthread_t unHilo;
	pthread_create(&unHilo,NULL,(void*) planificador, NULL);


	consola();


	pthread_join(unHilo, NULL);

	log_info(logger, "---------------------FIN---------------------");


	list_destroy_and_destroy_elements(listaPCB,(void*) PCB_destroy);
	list_destroy(listaCPUs);

	config_destroy(config);
	log_destroy(logger);

	return 0;
}

int cargaListaCPU(int socketCliente)
{
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

	fd_set readset;

	listenningSocket = recibirLlamada(PUERTO);

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

void recibirRespuesta(int socketCliente)
{
	t_respuesta respuesta;

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
		log_info(logger, "mProc %d - Iniciado", respuesta.pid);
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
				log_info(logger, "mProc %d - Pagina %d leida: %s", respuesta.pid, respuesta.paginas, respuesta.content);
			}
			else
			{
				if( (respuesta.mensajeSize)==3 )
				{
					log_info(logger, "mProc %d finalizado", respuesta.pid);
				}
				else
				{
					if( (respuesta.mensajeSize)==4 )
					{
						log_info(logger, "mProc %d - Pagina %d escrita: %s", respuesta.pid, respuesta.paginas, respuesta.content);
					}
				}
			}
		}
	}

	free(package);
	free(package2);
}

void ROUND_ROBIN(void* args)
{
	t_enviarProceso *mensaje;
	mensaje = (t_enviarProceso*) args;

	PCB* pcbReady = mensaje->pcbReady;
	int posPCB = mensaje->posPCB;

	sem_wait(&semCPU);
	pthread_mutex_lock(&mutex);
	t_hiloCPU* hiloCPU = buscarCPUDisponible(); //Busco algun CPU que este disponible
	int posCPU = encontrarPosicionHiloCPU(hiloCPU->idHilo); //Busco posicion del CPU disponible

	int socketCliente = hiloCPU->socketCliente;
	int idHiloCPU = hiloCPU->idHilo;

	list_replace_and_destroy_element(listaCPUs, posCPU, hiloCPU_create(idHiloCPU, socketCliente, 0), (void*) hiloCPU_destroy); //Pongo al CPU en ocupado (0)
	pthread_mutex_unlock(&mutex);

	int totalLineas = txt_total_lines(mensaje->file);
	txt_close_file(mensaje->file);

	int puntero = pcbReady->puntero;
	int i = pcbReady->puntero;
	int pid = pcbReady->pid;
	char* path = strdup(pcbReady->path);

	int Q = 0;

	while( ( (i-1)<=(totalLineas) ) && ( Q<QUANTUM ) )
	{
		pcbReady = buscarReadyEnPCB(pid);

		enviarPath(socketCliente, pid, pcbReady->path, pcbReady->puntero);

		pcbReady = buscarReadyEnPCB(pid);

		posPCB =  encontrarPosicionEnPCB(pid);	//Encontrar pos en listaPCB

		puntero = pcbReady->puntero;

		sem_wait(&semFZ);
		i=puntero+1;

		list_replace_and_destroy_element(listaPCB, posPCB, PCB_create(pid, path, (puntero+1), 1), (void*)PCB_destroy);

		sem_post(&semFZ);

		Q++;
	}

	if( (i-1)>(totalLineas) )
	{
		pthread_mutex_lock(&mutex2);
		list_remove_and_destroy_element(listaPCB, posPCB, (void*) PCB_destroy);
		pthread_mutex_unlock(&mutex2);
	}
	else
	{
		pthread_mutex_lock(&mutex4);
		list_add(listaReady, ready_create(pid));
		list_replace_and_destroy_element(listaPCB, posPCB, PCB_create(pid, path, i, 0), (void*)PCB_destroy);
		pthread_mutex_unlock(&mutex4);

		sem_post(&semPlani);
	}

	list_replace_and_destroy_element(listaCPUs, posCPU, hiloCPU_create(idHiloCPU, socketCliente, 1), (void*) hiloCPU_destroy);	//Pongo en Disponible al CPU q usaba
	sem_post(&semCPU);

	free(path);
	free(args);
}

void FIFO(void *args)
{
	t_enviarProceso *mensaje;
	mensaje = (t_enviarProceso*) args;

	PCB* pcbReady = mensaje->pcbReady;
	int posPCB = mensaje->posPCB;

	sem_wait(&semCPU);
	pthread_mutex_lock(&mutex);
	t_hiloCPU* hiloCPU = buscarCPUDisponible(); //Busco algun CPU que este disponible
	int posCPU = encontrarPosicionHiloCPU(hiloCPU->idHilo); //Busco posicion del CPU disponible

	int socketCliente = hiloCPU->socketCliente;
	int idHiloCPU = hiloCPU->idHilo;

	list_replace_and_destroy_element(listaCPUs, posCPU, hiloCPU_create(idHiloCPU, socketCliente, 0), (void*) hiloCPU_destroy); //Pongo al CPU en ocupado (0)
	pthread_mutex_unlock(&mutex);

	int totalLineas = txt_total_lines(mensaje->file);
	txt_close_file(mensaje->file);

	int puntero = pcbReady->puntero;
	int i = pcbReady->puntero;
	int pid = pcbReady->pid;
	char* path = strdup(pcbReady->path);

	while( (i-1)<=(totalLineas) )
	{
		pcbReady = buscarReadyEnPCB(pid);

		enviarPath(socketCliente, pid, pcbReady->path, pcbReady->puntero);

		pcbReady = buscarReadyEnPCB(pid);

		posPCB =  encontrarPosicionEnPCB(pid);	//Encontrar pos en listaPCB

		puntero = pcbReady->puntero;

		sem_wait(&semFZ);
		i=puntero+1;

		list_replace_and_destroy_element(listaPCB, posPCB, PCB_create(pid, path, (puntero+1), 1), (void*)PCB_destroy);

		sem_post(&semFZ);
	}

	list_replace_and_destroy_element(listaCPUs, posCPU, hiloCPU_create(idHiloCPU, socketCliente, 1), (void*) hiloCPU_destroy);	//Pongo en Disponible al CPU q usaba
	sem_post(&semCPU);

	pthread_mutex_lock(&mutex2);
	list_remove_and_destroy_element(listaPCB, posPCB, (void*) PCB_destroy);
	pthread_mutex_unlock(&mutex2);

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

		t_ready *unReady = list_remove(listaReady, 0);	//Busco al primer ready
		int pidReady = unReady->pid;
		ready_destroy(unReady);

		pthread_mutex_lock(&mutex3);
		PCB* pcbReady = buscarReadyEnPCB(pidReady);	//Busco al ready en el PCB

		int posPCB =  encontrarPosicionEnPCB(pidReady);	//Encontrar pos en listaPCB
		pthread_mutex_unlock(&mutex3);

		FILE* file = txt_open_for_read(pcbReady->path);

		pthread_t hilo;

		if (file == NULL)
		{
			log_warning(logger, "Path: %s Incorrecto :/", pcbReady->path);

			list_remove_and_destroy_element(listaPCB, posPCB, (void*) PCB_destroy);
		}
		else
		{
			t_enviarProceso *unaPersona;	//Creo la estructura a enviar

			unaPersona = (t_enviarProceso *)malloc(sizeof(t_enviarProceso));
			unaPersona->file = file;
			unaPersona->pcbReady = pcbReady;
			unaPersona->posPCB = posPCB;

			if (strncmp(ALGORITMO_PLANIFICACION,"FIFO", 4) == 0)
			{
				pthread_create(&hilo, NULL, (void*) FIFO, (void*) unaPersona);
			}
			else
			{
				pthread_create(&hilo, NULL, (void*) ROUND_ROBIN, (void*) unaPersona);
			}
		}
	}
}

void enviarPath(int socketCliente, int pid, char * path, int punteroProx)
{
	t_pathMensaje unaPersona;
	unaPersona.pid = pid;
	unaPersona.pathSize=strlen(path);
	unaPersona.path = strdup(path);
	unaPersona.puntero = punteroProx;

	void* package = malloc(tamanioEstructuraAEnviar(unaPersona));

	memcpy(package,&unaPersona.pid,sizeof(unaPersona.pid));
	memcpy(package+sizeof(unaPersona.pid),&unaPersona.puntero,sizeof(unaPersona.puntero));
	memcpy(package+sizeof(unaPersona.pid)+sizeof(unaPersona.puntero), &unaPersona.pathSize, sizeof(unaPersona.pathSize));
	memcpy(package+sizeof(unaPersona.pid)+sizeof(unaPersona.puntero)+sizeof(unaPersona.pathSize), unaPersona.path, unaPersona.pathSize);

	send(socketCliente,package, tamanioEstructuraAEnviar(unaPersona),0);

	recibirRespuesta(socketCliente);

	free(unaPersona.path);

	free(package);
}

void correrPath(char * pch)
{
	pid++;

	pthread_mutex_lock(&mutex4);
	list_add(listaReady, ready_create(pid)); //Agrego el proceso NUEVO a Ready

	list_add(listaPCB, PCB_create(pid, pch, 2, 0));	//Agrego el proceso NUEVO al PCB
	pthread_mutex_unlock(&mutex4);

	sem_post(&semPlani);

	log_info(logger, "Correr %s, mProc: %d", pch, pid);

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

	int i;

	for(i=0;i<list_size(listaCPUs);i++)
	{
		new2 = list_get(listaCPUs,i);

		printf("Disponible %d\n",new2->disponible);
		printf("Socket %d\n",new2->socketCliente);
		printf("PID %d\n",new2->idHilo);
	}
}
void finalizarPID(int pidF)
{
	int posPCB =  encontrarPosicionEnPCB(pidF);	//Encontrar pos en listaPCB
	PCB* unPCB = buscarPCB(pidF);

	FILE* file = txt_open_for_read(unPCB->path);

	if (file == NULL)
	{
		return;
	}
	else
	{
		totalLineas = txt_total_lines(file);

		sem_post(&semFZ);
		list_replace_and_destroy_element(listaPCB, posPCB, PCB_create(unPCB->pid,unPCB->path, totalLineas, 1), (void*) PCB_destroy);
		sem_wait(&semFZ);

		txt_close_file(file);
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

	        if (strncmp(pch,"cr", 3) == 0) //Correr PATH
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
	        	if (strncmp(pch, "cpu", 2) == 0)
	        	{
	        		CPU();

	        		continue;
	        	}
	        else
	        {
	        	if (strncmp(pch, "man", 2) == 0)
	        	{
	        		printf("--------------COMANDOS-------------- \n");
	        		printf("cr: CorreR PATH \n");
	        		printf("fz: FinaliZar PID \n");
	        		printf("ps: PS \n");
	        		printf("cpu: CPU \n");
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
	        		break;
	        	}
	        else
	        {
	        	printf("Error Comando \n");

	        	continue;
	        }//Fin error comando
	        }//Fin fin
	        }//Fin man
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
static PCB *PCB_create(int pid, char * path, int puntero, int estado)
{
	 PCB *new = malloc(sizeof(PCB));
	 new->path = strdup(path);
	 new->pid = pid;
	 new->puntero = puntero;
	 new->estado = estado;

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
