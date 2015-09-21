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

#include <commons/socket.h>
#include <commons/config.h>
#include <commons/collections/list.h>


#define BACKLOG 5			// Define cuantas conexiones vamos a mantener pendientes al mismo tiempo
#define PACKAGESIZE 1024	// Define cual va a ser el size maximo del paquete a enviar


typedef struct
{
    int idHilo;
    int socketCliente;
}t_hiloCPU;




t_list * cargarlista(t_list *listaCPUs, char * PUERTO);
int procesar(int socketCliente, t_list * listaCPUs);
void consola();




int main()
{
	printf("\n");
	printf("----PLANIFICADOR----\n\n");




	t_config* config;

	config = config_create("/home/utnso/github/tp-2015-2c-daft-punk-so/planificador/config.cfg");

	char * PUERTO_ESCUCHA = config_get_string_value(config, "PUERTO_ESCUCHA");

	t_list *listaCPUs;
	listaCPUs = list_create();


	//Recibo las conexiones de (hilos) CPU y las cargo a la listaCPUs
	cargarlista(listaCPUs,PUERTO_ESCUCHA);



	printf("\n");
	printf("Contenido de la listaCPUs\n");
	printf("\n");




	t_hiloCPU *new = malloc(sizeof(t_hiloCPU));

	int i;

	for (i = 0; i < list_size(listaCPUs); i++)
	{
		new = list_get(listaCPUs, i);
		printf("Id: %d\n", new->idHilo);
		printf("Socket: %d\n", new->socketCliente);

		close(new->socketCliente);
	};

	free(new);

	list_destroy(listaCPUs);




	//Consola!!!!!!!! :DD
	consola();


	config_destroy(config);

	return 0;
}




int tamanioMensaje1(t_hiloCPU mensaje)
{
    return sizeof(int);

};




static t_hiloCPU *mensaje_create(int idNodo, int socket)
{
    t_hiloCPU *new = malloc(sizeof(t_hiloCPU));
    new->idHilo = idNodo;
    new->socketCliente = socket;

    return new;
}




int procesar(int socketCliente, t_list * listaCPUs)
{

	int pnumero;

	t_hiloCPU mensaje;

	void* package=malloc(tamanioMensaje1(mensaje));

	recv(socketCliente,package,sizeof(int),0);
	memcpy(&mensaje.idHilo,package,sizeof(int));
	recv(socketCliente,package+sizeof(int),sizeof(int),0);
	memcpy(&pnumero,package+sizeof(int),sizeof(int));


	list_add(listaCPUs, mensaje_create(mensaje.idHilo,socketCliente));

	free(package);

	return pnumero;
};




t_list * cargarlista(t_list *listaCPUs, char * PUERTO)
{
	int pnumero, i=0;

	fd_set readset;
	int maxfd;
	int socketCliente, result;

	int listenningSocket;
	listenningSocket = recibirLlamada(PUERTO);

	FD_ZERO(&readset);
	FD_SET(listenningSocket, &readset);

	maxfd = listenningSocket;

	do
	{
		printf("Recibiendo llamada!! \n");
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
				pnumero = procesar(socketCliente, listaCPUs);


				i++;

			}

		}

	} while (i!=pnumero);


	close(listenningSocket);

	return(listaCPUs);
}




void consola()
{
    char comando[100];

    while(1)
    {
        printf("Ingrese un comando: ");
        scanf("%[^\n]%*c", comando);

        char * pch;

        pch = strtok(comando," ");

        if (strncmp(pch, "cr", 2) == 0)
        {
            printf("Correr PATH \n");

			pch = strtok(NULL," ");

           	printf("PATH: %s\n",pch);

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
                printf("PS \n");

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
