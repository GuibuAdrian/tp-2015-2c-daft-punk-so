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


#define BACKLOG 5			// Define cuantas conexiones vamos a mantener pendientes al mismo tiempo
#define PACKAGESIZE 1024	// Define cual va a ser el size maximo del paquete a enviar


typedef struct
{
    int idHilo;
    int socketCliente;
    int disponible; // 1 = Disponible, 0 = NO Disponible
}t_hiloCPU;

typedef struct
{
    int pid;
    char *path;
    int puntero;
    int estado;  // 0 = Listo,  1 = Ejecutando,  2 = Bloqueado
}PCB;


typedef struct
{
    int pid;

}t_ready;

sem_t x;
t_list *listaCPUs;
t_list *listaPCB;
t_list *listaReady;
int pid = 0;
char *ALGORITMO_PLANIFICACION;


int tamanioMensaje1(t_hiloCPU mensaje)
{
    return sizeof(int)+sizeof(int);

};
static t_hiloCPU *mensaje_create(int idNodo, int socket)
{
    t_hiloCPU *new = malloc(sizeof(t_hiloCPU));
    new->idHilo = idNodo;
    new->socketCliente = socket;
    new->disponible = 1;

    return new;
}
static void hilosCPU_destroy(t_hiloCPU *self)
{
    free(self);
}
int tamanioHiloCPU(t_hiloCPU mensaje)
{
    return sizeof(mensaje.disponible)+sizeof(mensaje.idHilo)+sizeof(mensaje.socketCliente);

};
static PCB *PCB_create(int pid, char *path)
{
	 PCB *new = malloc(sizeof(PCB));
	 new->path = strdup(path);
	 new->pid = pid;
	 new->puntero = 0;
	 new->estado = 0;

	 return new;
}
static void PCB_destroy(PCB *self)
{
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

void recibirConexiones(char * PUERTO);
int cargaListaCPU(int socketCliente, t_list * listaCPUs);
void cerrarConexiones();
void consola();
void planificador();

void verListaCPUs();
void correrPath(char * pch);
void PS();





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



	//Recibo las conexiones de (hilos) CPU y las cargo a la listaCPUs
	recibirConexiones(PUERTO_ESCUCHA);


	pthread_t unHilo;
	pthread_create(&unHilo,NULL,(void*) planificador, NULL);




	//Consola!!!!!!!! :DD
	consola();




	cerrarConexiones();




	pthread_join(unHilo, NULL);

	list_destroy_and_destroy_elements(listaCPUs,(void*) hilosCPU_destroy);
    list_destroy_and_destroy_elements(listaPCB, (void*) PCB_destroy);
    list_destroy_and_destroy_elements(listaReady, (void*) ready_destroy);

	config_destroy(config);


	return 0;
}





int cargaListaCPU(int socketCliente, t_list * listaCPUs)
{
	int cantHilos;

	t_hiloCPU mensaje;

	void* package=malloc(tamanioMensaje1(mensaje));

	recv(socketCliente,package,sizeof(int),0);
	memcpy(&mensaje.idHilo,package,sizeof(int));
	recv(socketCliente,package+sizeof(int),sizeof(int),0);
	memcpy(&cantHilos,package+sizeof(int),sizeof(int));


	//Cargo la lista con los sockets CPU
	list_add(listaCPUs, mensaje_create(mensaje.idHilo,socketCliente));

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
				cantHilos = cargaListaCPU(socketCliente, listaCPUs);


				i++;

			}

		} //Fin else if  (result > 0)

	} while (i!=cantHilos);


	close(listenningSocket);
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

	        pch = strtok(comando," ");

	        if (strncmp(pch,"cr", 3) == 0)
	        {
	        	//Correr PATH

	        	pch = strtok(NULL," ");

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
                //PS

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

                break;
            }
        else
        {
            printf("Error Comando \n");

            continue;
        }
        }//Fin fin
        }//Fin man
        }//Fin cpu
        }//Fin ps
        }//Fin fz
    }//Fin while
}//Fin main




void correrPath(char * pch)
{
	printf("Correr PATH\n");

	char * ch = malloc(PACKAGESIZE);

	pid++;

	//Agrego un proceso al PCB
	list_add(listaPCB, PCB_create(pid, pch));
	//Agrego un proceso a ready
	list_add(listaReady, ready_create(pid));

	printf("%s\n", pch);

	sem_post(&x);

	free(ch);


	printf("\n");
}
void PS()
{
	PCB mensaje;
	PCB *new = malloc(tamanioPCB(mensaje));

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

	free(new);
}
void verListaCPUs()
{
	t_hiloCPU *new = malloc(sizeof(t_hiloCPU));

	int i;

	for (i = 0; i < list_size(listaCPUs); i++)
	{
		new = list_get(listaCPUs, i);
		printf("Id: %d\n", new->idHilo);
		printf("Socket: %d\n", new->socketCliente);

		if(new->disponible==1)
		{
			printf("Disponible: SI\n");
		}
		else
		{
			printf("Disponible: NO\n");
		}

	};

	free(new);
}


t_hiloCPU* buscarDisponible()
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



void FIFO()
{
	t_ready mensaje;
	t_ready *unReady = malloc(tamanioready(mensaje));

	unReady = list_get(listaReady, 0);

	t_hiloCPU* aux = buscarDisponible();


	bool compararPorIdentificador2(PCB *unaCaja)
	{
		if (unaCaja->pid == unReady->pid)
		{
			return 1;
		}

		return 0;
	}

	PCB* aux2 = list_find(listaPCB, (void*) compararPorIdentificador2);

	send(aux->socketCliente, aux2->path, strlen(aux2->path) + 1, 0);

	list_remove(listaReady, 0);

	list_remove_by_condition(listaReady, (void*) compararPorIdentificador2);

	free(unReady);
}
void planificador()
{
	while(list_size(listaCPUs) != 0)
	{
		sem_wait(&x);


		if (strncmp(ALGORITMO_PLANIFICACION,"FIFO", 4) == 0)
		{
			FIFO();
		}

	}

}




void cerrarConexiones()
{
	printf("\n Cerrando conexiones! \n");

	int i;
	t_hiloCPU mensaje;
	t_hiloCPU *new2 = malloc(tamanioHiloCPU(mensaje));

	for (i = 0; i < list_size(listaCPUs); i++)
	{
		new2 = list_get(listaCPUs, i);

		close(new2->socketCliente);
	};

	free(new2);
}
