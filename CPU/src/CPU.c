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

#define BACKLOG 5			// Define cuantas conexiones vamos a mantener pendientes al mismo tiempo
#define PACKAGESIZE 1024	// Define cual va a ser el size maximo del paquete a enviar

typedef struct
{
    int idNodo;
    int cantHilos;
}t_idHilo;

typedef struct
{
	char *ipPlanificador;
	char *puertoPlanificador;
	char *ipMemoria;
	char *puertoMemoria;
    int numeroHilos;
}tspaghetti;

typedef struct
{
	int pid;
    int pathSize;
    char path[PACKAGESIZE];
    int puntero;
}t_pathMensaje;

typedef struct
{
	int pid;
	int paginas;
	int respuestaSize;
}t_respuesta;

typedef struct
{
	int pid;
	int paginas;
	int mensajeSize;
}t_respuesta_Planificador;

typedef struct
{
	int pid;
	int orden;	// 0=Iniciar, 1=Leer, 2=Escribir, 3=Finalizar
	int pagina;
}t_orden_memoria;


int id1 = 5;
int RETARDO, socketMemoria, socketPlanificador;
t_log* logger;

int tamanioEstructura2(t_pathMensaje unaPersona);
int tamanioRespuesta(t_respuesta unaRespuesta);

void conectarHilos();
void recibirPath(int serverSocket);
char * obtenerLinea(char path[PACKAGESIZE], int puntero);
void enviarRespuesta(int socketPlanificador, int pid, int resultado, int pagina);
void enviarAMemoria(int orden, int pagina, int pid);
void interpretarInstruccion(int serverSocket, t_pathMensaje unaPersona, char * linea);
t_respuesta_Planificador recibirRespuestaMemoria();




int main() {
	printf("\n");
	printf("----CPU----\n\n");


	logger = log_create("/home/utnso/github/tp-2015-2c-daft-punk-so/CPU/logsTP", "CPU",true, LOG_LEVEL_INFO);



	t_config* config;

	config = config_create("/home/utnso/github/tp-2015-2c-daft-punk-so/CPU/config.cfg");

	tspaghetti spaghetti;

	spaghetti.ipPlanificador = config_get_string_value(config, "IP_PLANIFICADOR");
	spaghetti.puertoPlanificador = config_get_string_value(config,"PUERTO_PLANIFICADOR");
	spaghetti.ipMemoria = config_get_string_value(config, "IP_MEMORIA");
	spaghetti.puertoMemoria = config_get_string_value(config,"PUERTO_MEMORIA");
	spaghetti.numeroHilos = config_get_int_value(config, "CANTIDAD_HILOS");
	RETARDO = config_get_int_value(config, "RETARDO");

	int i = 1;

	while(i<=spaghetti.numeroHilos)
	{
		log_info(logger, "CPU id: %d creado", id1);

		pthread_t unHilo;
		pthread_create(&unHilo,NULL,(void*) conectarHilos, &spaghetti);


		pthread_join(unHilo, NULL);

		i++;
	}

	log_info(logger, "Cerrando conexiones");

	log_info(logger, "-------------------------------------------------");


	close(socketMemoria);

    log_destroy(logger);

	return 0;
}

void conectarHilos(void *context)
{
	tspaghetti *spaghetti = context;



	socketPlanificador = conectarse(spaghetti->ipPlanificador, spaghetti->puertoPlanificador);
    socketMemoria = conectarse(spaghetti->ipMemoria, spaghetti->puertoMemoria);
	log_info(logger, "Conectado a Memoria. CPU: %d", id1);


    t_idHilo mensaje;

    id1++;
    mensaje.idNodo = id1;
    mensaje.cantHilos = spaghetti->numeroHilos;

    //Envio id y aviso la cantidad de conexiones
    void* package = malloc(sizeof(t_idHilo));

    memcpy(package,&mensaje.idNodo,sizeof(mensaje.idNodo));
    memcpy(package+sizeof(mensaje.idNodo),&mensaje.cantHilos,sizeof(mensaje.cantHilos));

    send(socketPlanificador,package, sizeof(t_idHilo), 0);

    free(package);

    recibirPath(socketPlanificador);

    close(socketPlanificador);

}

void recibirPath(int serverSocket)
{
	int status = 1;

	t_pathMensaje unaPersona;

	void* package = malloc(tamanioEstructura2(unaPersona));


	while(status)
	{
		status = recv(serverSocket, (void*)package, sizeof(unaPersona.pid), 0);


		if(status == 0)
		{
			break;
		}
		else
		{
			memcpy(&unaPersona.pid,package,sizeof(unaPersona.pid));
			recv(serverSocket,(void*) (package+sizeof(unaPersona.pid)), sizeof(unaPersona.puntero), 0);
			memcpy(&unaPersona.puntero,package+sizeof(unaPersona.pid),sizeof(unaPersona.puntero));//--
			recv(serverSocket,(void*) (package+sizeof(unaPersona.pid)+sizeof(unaPersona.puntero)), sizeof(unaPersona.pathSize), 0);
			memcpy(&unaPersona.pathSize,package+sizeof(unaPersona.pid)+sizeof(unaPersona.puntero),sizeof(unaPersona.pathSize));//--

			void* package2=malloc(unaPersona.pathSize);

			recv(serverSocket,(void*) package2, unaPersona.pathSize, 0);
			memcpy(&unaPersona.path, package2,unaPersona.pathSize);

			log_info(logger, "mProc %d recibido", unaPersona.pid);
			log_info(logger, "Path %s recibido", unaPersona.path);

			char * linea = obtenerLinea(unaPersona.path, unaPersona.puntero);

			interpretarInstruccion(serverSocket, unaPersona, linea);


			sleep(RETARDO);

			free(linea);
			free(package2);
		}

	}

	free(package);
}

void enviarRespuesta(int socketPlanificador, int pid, int resultado, int pagina)
{
	t_respuesta respuesta;

	void* package = malloc(sizeof(respuesta.pid)+sizeof(respuesta.paginas)+sizeof(respuesta.respuestaSize));

	recv(socketMemoria,(void*)package, sizeof(respuesta.pid), 0);
	memcpy(&respuesta.pid,package,sizeof(respuesta.pid));

	recv(socketMemoria,(void*) (package+sizeof(respuesta.pid)), sizeof(respuesta.paginas), 0);
	memcpy(&respuesta.paginas, package+sizeof(respuesta.pid),sizeof(respuesta.paginas));

	recv(socketMemoria,(void*) (package+sizeof(respuesta.pid)+sizeof(respuesta.paginas)), sizeof(respuesta.respuestaSize), 0);
	memcpy(&respuesta.respuestaSize, package+sizeof(respuesta.pid)+sizeof(respuesta.paginas),sizeof(respuesta.respuestaSize));

	log_info(logger, "Mensaje: %d", respuesta.respuestaSize);

	send(socketPlanificador, package, tamanioRespuesta(respuesta),0);

	free(package);
}

char * obtenerLinea(char path[PACKAGESIZE], int puntero)
{
	FILE* file;
	file = txt_open_for_read(path);


	//printf("Linea: %d\n", puntero);

	char * linea = read_line(file, puntero-1);

	txt_close_file(file);

	log_info(logger, "Linea obtenida");

	return linea;
}
void enviarAMemoria(int orden, int pagina, int pid)
{
	printf("Ejecute orden: %d\n",orden);

	t_orden_memoria orden_memoria;

	void* ordenPackage = malloc( (sizeof(int)+sizeof(int)+sizeof(int)) );

	orden_memoria.pid = pid;
	orden_memoria.orden = orden;
	orden_memoria.pagina = pagina;


	memcpy(ordenPackage,&orden_memoria.pid,sizeof(orden_memoria.pid));
	memcpy(ordenPackage+sizeof(pid),&orden_memoria.orden,sizeof(orden_memoria.orden));
	memcpy(ordenPackage+sizeof(pid)+sizeof(pagina), &orden_memoria.pagina, sizeof(orden_memoria.pagina));


	send(socketMemoria, ordenPackage, sizeof(int)+sizeof(int)+sizeof(int),0);

	free(ordenPackage);
}
void interpretarInstruccion(int serverSocket, t_pathMensaje unaPersona, char * linea)
{
	char * pch = strtok(linea," \n");
	int pagina;

	if (strncmp(pch,"iniciar", 7) == 0)
	{

		pch = strtok(NULL," \n");
    	pagina = strtol(pch, NULL, 10);

    	enviarAMemoria(0, pagina, unaPersona.pid);

    	log_info(logger, "Ejecutando Iniciar. PID: %d . Parametro/s: %d", unaPersona.pid, pagina);

    	t_respuesta_Planificador respuesta = recibirRespuestaMemoria();

		enviarRespuesta(serverSocket, respuesta.pid, respuesta.mensajeSize, -1);

	}
	else
	{
		if (strncmp(pch, "leer", 4) == 0)
		{
			pch = strtok(NULL," \n");
			pagina = strtol(pch, NULL, 10);

			enviarAMemoria(1, pagina, unaPersona.pid);

			log_info(logger, "Ejecutado Leer. PID: %d . Parametro/s: %d", unaPersona.pid, pagina);

	    	t_respuesta_Planificador respuesta = recibirRespuestaMemoria();

			enviarRespuesta(serverSocket,respuesta.pid, respuesta.mensajeSize, respuesta.paginas);


		}
	else
	{
		if (strncmp(pch, "finalizar", 9) == 0)
		{
			enviarAMemoria(3,0 , unaPersona.pid);

			log_info(logger, "Ejecutado Finalizar. PID: %d", unaPersona.pid);

	    	t_respuesta_Planificador respuesta = recibirRespuestaMemoria();

			enviarRespuesta(serverSocket,respuesta.pid, respuesta.mensajeSize, -1);


		}
	} //else finalizar
	} //else leer
}


t_respuesta_Planificador recibirRespuestaMemoria()
{
	log_info(logger, "Esperando resultado");

	t_respuesta_Planificador respuesta;

	void* package = malloc(sizeof(respuesta.pid)+sizeof(respuesta.paginas)+sizeof(respuesta.mensajeSize));

	recv(socketMemoria,(void*)package, sizeof(respuesta.pid), 0);
	memcpy(&respuesta.pid,package,sizeof(respuesta.pid));

	recv(socketMemoria,(void*) (package+sizeof(respuesta.pid)), sizeof(respuesta.paginas), 0);
	memcpy(&respuesta.paginas, package+sizeof(respuesta.pid),sizeof(respuesta.paginas));

	recv(socketMemoria,(void*) (package+sizeof(respuesta.pid)+sizeof(respuesta.paginas)), sizeof(respuesta.mensajeSize), 0);
	memcpy(&respuesta.mensajeSize, package+sizeof(respuesta.pid)+sizeof(respuesta.paginas),sizeof(respuesta.mensajeSize));

	//EnviarRespuestaCPU(package, respuesta, socketCPU);

	log_info(logger, "Mensaje: %d", respuesta.mensajeSize);

	free(package);

	return respuesta;
}

int tamanioEstructura2(t_pathMensaje unaPersona)
{
	return (sizeof(unaPersona.puntero)+sizeof(unaPersona.pathSize)+strlen(unaPersona.path));
};
int tamanioRespuesta(t_respuesta unaRespuesta)
{
	return (sizeof(unaRespuesta.respuestaSize) + sizeof(unaRespuesta.pid) + unaRespuesta.respuestaSize);
};

