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

#define BACKLOG 5			// Define cuantas conexiones vamos a mantener pendientes al mismo tiempo
#define PACKAGESIZE 1024	// Define cual va a ser el size maximo del paquete a enviar

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
    int puntero;
    int pathSize;
    char * path;
}t_mensaje2;

typedef struct
{
	int respuestaSize;
	char respuesta[PACKAGESIZE];
}t_respuesta;

t_list *listaCPUs;
t_list *listaPCB;
t_list *listaReady;
char *ALGORITMO_PLANIFICACION;
int pid=2;
sem_t x;

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
int tamanioEstructuraAEnviar(t_mensaje2 unaPersona);
int tamanioRespuesta(t_respuesta unaRespuesta)
{
	return (unaRespuesta.respuestaSize);
};

t_hiloCPU* buscarCPUDisponible();
PCB* buscarReadyEnPCB(t_ready* unReady);
int encontrarPosicionEnReady(int pid);
int encontrarPosicionEnPCB(int pid);
int encontrarPosicionHiloCPU(int idHilo);
void mostrarCPU();
void mostrarPCB();

void consola();
void recibirConexiones(char * PUERTO);
void cerrarConexiones();
void ROUND_ROBIN();
void FIFO();
void planificador();

int main()
{
	printf("\n");
	printf("----PLANIFICADOR----\n\n");

	listaCPUs = list_create();
	listaPCB = list_create();
	listaReady = list_create();

	sem_init(&x, 0, 0);

	t_config* config;

	config = config_create("/home/utnso/github/tp-2015-2c-daft-punk-so/planificador/config.cfg");

	char * PUERTO_ESCUCHA = config_get_string_value(config, "PUERTO_ESCUCHA");
	ALGORITMO_PLANIFICACION = config_get_string_value(config, "ALGORITMO_PLANIFICACION");



	recibirConexiones(PUERTO_ESCUCHA);



	pthread_t unHilo;
	pthread_create(&unHilo,NULL,(void*) planificador, NULL);


	consola();




	pthread_join(unHilo, NULL);

	cerrarConexiones();

	free(ALGORITMO_PLANIFICACION);
	free(PUERTO_ESCUCHA);

	list_destroy_and_destroy_elements(listaCPUs,(void*) hiloCPU_destroy);
	list_destroy_and_destroy_elements(listaPCB,(void*) PCB_destroy);
	list_destroy_and_destroy_elements(listaReady,(void*) ready_destroy);
	config_destroy(config);

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


	//Cargo la lista con los sockets CPU
	list_add(listaCPUs, hiloCPU_create(mensaje.idHilo,socketCliente, 1));

	free(package);

	return cantHilos;
};
void recibirConexiones(char * PUERTO)
{
	int cantHilos, i=0;
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
			printf("Error in select(): %s\n", strerror(errno));
		}
		else if (result > 0)
		{
			if (FD_ISSET(listenningSocket, &readset))
			{
				socketCliente = aceptarLlamada(listenningSocket);

				if (socketCliente < 0)
				{
					printf("Error in accept(): %s\n", strerror(errno));
				}

				//Identifico el Nodo
				cantHilos = cargaListaCPU(socketCliente);


				i++;

			}

		} //Fin else if  (result > 0)

	} while (i!=cantHilos);


	close(listenningSocket);
}


void recibirRespuesta(int socket)
{
	t_respuesta respuesta;

	void* package = malloc(sizeof(respuesta.respuestaSize));

	recv(socket,(void*)package, sizeof(respuesta.respuestaSize), 0);
	memcpy(&respuesta.respuestaSize,package,sizeof(respuesta.respuestaSize));

	void* package2=malloc(tamanioRespuesta(respuesta));

	recv(socket,(void*) package2, respuesta.respuestaSize, 0);
	memcpy(&respuesta.respuesta, package2, respuesta.respuestaSize);

	//printf("%s\n", respuesta.respuesta);

	free(package);
	free(package2);

}

void ROUND_ROBIN()
{
	printf("BATMAN y <ROUND_>ROBIN\n");
}
void enviarPath(int socketCliente, char * path, int punteroProx)
{
	t_mensaje2 unaPersona;
	unaPersona.puntero = punteroProx;
	unaPersona.pathSize=strlen(path);
	unaPersona.path = strdup(path);

	void* package = malloc(tamanioEstructuraAEnviar(unaPersona));

	memcpy(package,&unaPersona.puntero,sizeof(unaPersona.puntero));
	memcpy(package+sizeof(unaPersona.puntero), &unaPersona.pathSize, sizeof(unaPersona.pathSize));
	memcpy(package+sizeof(unaPersona.puntero)+sizeof(unaPersona.pathSize), unaPersona.path, unaPersona.pathSize);

	send(socketCliente,package, tamanioEstructuraAEnviar(unaPersona),0);

	recibirRespuesta(socketCliente);

	free(unaPersona.path);

	free(package);
}

void FIFO()
{
	t_ready *unReady;
	unReady = list_get(listaReady, 0);	//Busco al primer ready

	PCB* pcbReady = buscarReadyEnPCB(unReady);	//Busco al ready en el PCB

	int posPCB =  encontrarPosicionEnPCB(pcbReady->pid);	//Encontrar pos en listaPCB

	FILE* file = txt_open_for_read(pcbReady->path);

	if (file == NULL)
	{
		list_remove_and_destroy_element(listaPCB, posPCB, (void*) PCB_destroy);
		list_remove_and_destroy_element(listaReady, 0, (void*) ready_destroy);

		return;
	}
	else
	{
		//Busco algun CPU que este disponible
		t_hiloCPU* hiloCPU = buscarCPUDisponible();

		//Busco posicion del CPU disponible
		int posCPU = encontrarPosicionHiloCPU(hiloCPU->idHilo);

		//Pongo al CPU en ocupado (0)
		t_hiloCPU* aux = list_replace(listaCPUs, posCPU, hiloCPU_create(hiloCPU->idHilo,hiloCPU->socketCliente, 0));

		int totalLineas = txt_total_lines(file);
		txt_close_file(file);

		int i = pcbReady->puntero-1;

		while( i<=(totalLineas) )
		{
			enviarPath(hiloCPU->socketCliente, pcbReady->path, i+1);

			list_replace(listaPCB, posPCB, PCB_create(pcbReady->pid,pcbReady->path, (pcbReady->puntero+1), 1));

			i++;

		}


		list_replace_and_destroy_element(listaCPUs, posCPU, hiloCPU_create(hiloCPU->idHilo, hiloCPU->socketCliente, 1), (void*) hiloCPU_destroy);	//Pongo en Disponible al CPU q usaba
		list_remove_and_destroy_element(listaPCB, posPCB, (void*) PCB_destroy);
		list_remove_and_destroy_element(listaReady, 0, (void*) ready_destroy);

		hiloCPU_destroy(aux);
	}


}
void planificador()
{
	while(1)
	{
		sem_wait(&x);
		if(list_size(listaCPUs) == 0)
		{
			break;
		}

		if (strncmp(ALGORITMO_PLANIFICACION,"FIFO", 4) == 0)
		{
			FIFO();
		}
		else
		{
			ROUND_ROBIN();
		}
	}
}

void correrPath(char * pch)
{
	//printf("Correr PATH\n");

	pid++;

	//Agrego un proceso al PCB
	list_add(listaPCB, PCB_create(pid, pch, 2, 0));
	//Agrego un proceso a ready
	list_add(listaReady, ready_create(pid));

	//printf("%s\n", pch);

	sem_post(&x);

	printf("\n");
}
void PS()
{
	printf("Correr PS\n");

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

	        if (strncmp(pch,"cr", 3) == 0)
	        {
	        	//Correr PATH

	        	pch = strtok(NULL," \n");

	        	correrPath(pch);

	        	continue;
	        }
	        else
	        {
	        	if (strncmp(pch, "fz", 2) == 0)
	        	{
	        		printf("Finalizar PID \n");

	        		continue;
	        	}
	        else
	        {
	        	if (strncmp(pch, "ps", 2) == 0)
	        	{
	        		//Correr PS

	        		PS();

	        		continue;
	        	}
	        else
	        {
	        	if (strncmp(pch, "cpu", 2) == 0)
	        	{
	        		printf("CPU \n");


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

	        		int j;

	        		for(j=0;j<=list_size(listaCPUs);j++)
	        		{
	        			list_remove_and_destroy_element(listaCPUs, j, (void*) hiloCPU_destroy);
	        		}


	        		sem_post(&x);
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
void cerrarConexiones()
{
	printf("\n Cerrando conexiones! \n");

	int i;
	t_hiloCPU *unHilo;

	for (i = 0; i < list_size(listaCPUs); i++)
	{
		unHilo = list_get(listaCPUs, i);

		close(unHilo->socketCliente);
	};

}

void mostrarPCB()
{
	PCB* new2;

		int i;

		for(i=0;i<list_size(listaPCB);i++)
		{
			new2 = list_get(listaPCB,i);

			printf("Estado %d\n",new2->estado);
			printf("Path %s\n",new2->path);
			printf("PID %d\n",new2->pid);

		}
}
void mostrarCPU()
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
int tamanioEstructuraAEnviar(t_mensaje2 unaPersona)
{
	return (sizeof(unaPersona.puntero)+sizeof(unaPersona.pathSize)+strlen(unaPersona.path));
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
PCB* buscarReadyEnPCB(t_ready* unReady)
{
	bool compararPorIdentificador2(PCB *unaCaja)
	{
		if (unaCaja->pid == unReady->pid)
		{
			return 1;
		}

		return 0;
	}

	return list_find(listaPCB, (void*) compararPorIdentificador2);
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
