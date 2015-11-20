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
	int idCPU;
	int cantInst;
	int socketPan; //Le puse Pan aproposito :P
} t_cpu;

typedef struct
{
	int pathSize;
	char path[PACKAGESIZE];
} t_mensaje;

t_log* logger;
t_list *listaHilos, *listaCPUs, *listaRespuestas;
char *ipPlanificador, *puertoPlanificador, *ipMemoria, *puertoMemoria;
int id = 7, RETARDO, numeroHilos, socketPlanCarga;
pthread_mutex_t mutex, mutex2;

static t_hilos *hilo_create(pthread_t unHilo);
static void hilo_destroy(t_hilos *self);
static t_cpu *cpu_create(int idCPU, int cantInst, int socketPlan);
static void cpu_destroy(t_cpu *self);
static t_orden_CPU *respuesta_create(int pid, int orden, int pagina, char content[PACKAGESIZE]);
static void respuesta_destroy(t_orden_CPU *self);
int tamanioEstructura1(t_pathMensaje unaPersona);
int tamanioEstructura2(t_pathMensaje unaPersona);
int tamanioMensajeMemo(t_orden_CPU mensajeMemo);

t_orden_CPU* buscarPID(int pid);
t_cpu* buscarCPU(int sock);
int encontrarPosicionCPU(int sock);

void conectarHilos1();
void recibirPath1(int serverSocket, int idNodo);
char * obtenerLinea(char path[PACKAGESIZE], int puntero);
void interpretarLinea(int socketPlanificador, char* linea, int pid, int idNodo);
char * obtenerLinea(char path[PACKAGESIZE], int puntero);
t_orden_CPU enviarOrdenAMemoria(int pid, int orden, int paginas, char *content, int idNodo);
void cargaCPU();
void recibirSolicitudCarga_finQ();
void removerPID(int pid);
void enviarRespuestas(int socketPlanificador, int pid);

int main()
{
	printf("\n");
	printf("~~~~~~~~~~CPU~~~~~~~~~~\n\n");


	listaHilos = list_create();
	listaCPUs = list_create();
	listaRespuestas = list_create();

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
	pthread_t unHilo, otroHilo;
	pthread_mutex_init(&mutex, NULL);
	pthread_mutex_init(&mutex2, NULL);

	socketPlanCarga = conectarse(ipPlanificador, puertoPlanificador);


	while (i <= numeroHilos)
	{
		pthread_create(&unHilo, NULL, (void*) conectarHilos1, NULL);

		list_add(listaHilos,hilo_create(unHilo));

		i++;
	}

	pthread_create(&otroHilo,NULL,(void*) cargaCPU, NULL); //Hilo para recibir solicitud de cargas

	int j;
	t_hilos* new;

	for(j=0; j<list_size(listaHilos); j++)
	{
		new = list_get(listaHilos,j);

		pthread_join(new->unHilo, NULL);
	}

	pthread_join(otroHilo, NULL);
	list_destroy_and_destroy_elements(listaHilos,(void*) hilo_destroy);
	list_destroy_and_destroy_elements(listaCPUs,(void*) cpu_destroy);

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
	pthread_mutex_lock(&mutex);
	id++;
	list_add(listaCPUs,cpu_create(id,0,serverSocket));

	pthread_mutex_unlock(&mutex);
	mensaje.idNodo = id;
	mensaje.cantHilos = numeroHilos;

	//Envio id y aviso la cantidad de conexiones
	void* package = malloc(sizeof(t_idHilo));

	memcpy(package, &mensaje.idNodo, sizeof(mensaje.idNodo));
	memcpy(package + sizeof(mensaje.idNodo), &mensaje.cantHilos, sizeof(mensaje.cantHilos));

	send(serverSocket, package, sizeof(t_idHilo), 0);

	free(package);

	recibirPath1(serverSocket, mensaje.idNodo);

	close(serverSocket);

}

void cargaCPU()
{
	log_info(logger,"Conectado CPU %d!!", socketPlanCarga);

	recibirSolicitudCarga_finQ();

	close(socketPlanCarga);
}

void recibirSolicitudCarga_finQ()
{
	int package;
	int status = 1; // Estructura que manjea el status de los recieve.

	int i;
	t_cpu *new;

	while (status != 0)
	{
		status = recv(socketPlanCarga, &package, sizeof(int), 0);

		if(package == 1)
		{
			if (status != 0)
			{
				for(i=0; i<list_size(listaCPUs)+1; i++)
				{
					pthread_mutex_lock(&mutex);

					t_idHilo mensaje;

					if(i<list_size(listaCPUs))
					{
						new = list_get(listaCPUs,i);
						mensaje.idNodo = new->idCPU;
						mensaje.cantHilos = new->cantInst;
					}
					else
					{
						mensaje.idNodo = -1;
						mensaje.cantHilos = -1;
					}

					//Envio id y aviso la cantidad de conexiones
					void* package = malloc(sizeof(t_idHilo));

					memcpy(package, &mensaje.idNodo, sizeof(mensaje.idNodo));
					memcpy(package + sizeof(mensaje.idNodo), &mensaje.cantHilos, sizeof(mensaje.cantHilos));

					send(socketPlanCarga, package, sizeof(t_idHilo), 0);

					free(package);
					pthread_mutex_unlock(&mutex);
				}
			}

		}
		else
		{
			int pid;

			recv(socketPlanCarga, &pid, sizeof(int), 0);

			log_info(logger, "Rafaga concluida. PID %d", pid);

			enviarRespuestas(socketPlanCarga, pid);
		}

	};
}

void recibirPath1(int serverSocket, int idNodo)
{
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

			strncpy(unaPersona.path, " ", PACKAGESIZE);

			pthread_mutex_lock(&mutex2);

			log_info(logger,"Recibido mProc: %d, path: %s, puntero: %d", unaPersona.pid, unaPersona.path, unaPersona.puntero);

			interpretarLinea(serverSocket,linea, unaPersona.pid, idNodo);

			pthread_mutex_unlock(&mutex2);

			pthread_mutex_lock(&mutex);
			int posCPU = encontrarPosicionCPU(serverSocket);
			t_cpu* new = buscarCPU(serverSocket);

			list_replace_and_destroy_element(listaCPUs, posCPU, cpu_create(new->idCPU, new->cantInst+1, new->socketPan) ,(void*) cpu_destroy);

			pthread_mutex_unlock(&mutex);

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

	send(socketPlanificador, respuestaPackage, tamanioMensajeMemo(respuestaPlan), 0);

	free(respuestaPackage);
}

void enviarRespuestas(int socketPlanificador, int pid)
{
	int i;
	t_orden_CPU *new;

	for(i=0; i < list_size(listaRespuestas); i++)
	{
		new = list_get(listaRespuestas, i);
		if(new->pid == pid)
		{
			enviarRespuestaPlanificador(socketPlanificador, new->pid, new->orden, new->pagina, new->content);
		}
	}

	enviarRespuestaPlanificador(socketPlanificador, -1, -1, -1, "/");

	removerPID(pid);
}


t_orden_CPU enviarOrdenAMemoria(int pid, int orden, int paginas, char *content, int idNodo)
{
	t_orden_CPU mensajeMemoria;

	mensajeMemoria.pid = pid;
	mensajeMemoria.orden = orden;
	mensajeMemoria.pagina = paginas;
	mensajeMemoria.contentSize = strlen(content)+1;
	strncpy(mensajeMemoria.content,content, mensajeMemoria.contentSize);

	int socketMemoria = conectarse(ipMemoria, puertoMemoria);

	void* mensajeMemoPackage = malloc(tamanioMensajeMemo(mensajeMemoria));

	memcpy(mensajeMemoPackage, &mensajeMemoria.pid, sizeof(mensajeMemoria.pid));
	memcpy(mensajeMemoPackage+sizeof(mensajeMemoria.pid), &mensajeMemoria.orden, sizeof(mensajeMemoria.orden));
	memcpy(mensajeMemoPackage+sizeof(mensajeMemoria.pid)+sizeof(mensajeMemoria.orden), &mensajeMemoria.pagina, sizeof(mensajeMemoria.pagina));
	memcpy(mensajeMemoPackage+sizeof(mensajeMemoria.pid)+sizeof(mensajeMemoria.orden)+sizeof(mensajeMemoria.pagina), &mensajeMemoria.contentSize, sizeof(mensajeMemoria.contentSize));
	memcpy(mensajeMemoPackage+sizeof(mensajeMemoria.pid)+sizeof(mensajeMemoria.orden)+sizeof(mensajeMemoria.pagina)+sizeof(mensajeMemoria.contentSize), &mensajeMemoria.content, mensajeMemoria.contentSize);

	log_info(logger, "ID CPU: %d conectado a Memoria", idNodo);

	send(socketMemoria, mensajeMemoPackage, tamanioMensajeMemo(mensajeMemoria), 0);

	free(mensajeMemoPackage);

	return recibirRespuestaSwap(socketMemoria);
}

void interpretarLinea(int socketPlanificador, char* linea, int pid, int idNodo)
{
	char * pch = strtok(linea, " \n");
	int pagina;
	t_orden_CPU mensaje;

	if (strncmp(pch, "finalizar", 9) == 0)
	{
		mensaje = enviarOrdenAMemoria(pid, 3, 0, "/", idNodo);
	}
	else
	{
		if (strncmp(pch, "entrada-salida", 9) == 0)
		{
			pch = strtok(NULL, " \n");
			pagina = strtol(pch, NULL, 10);

			mensaje.pid = pid;
			mensaje.pagina = pagina;
			mensaje.orden = 5;
			strcpy(mensaje.content,"/");
		}
		else
		{
			if (strncmp(pch, "iniciar", 7) == 0)
			{
				pch = strtok(NULL, " \n");
				pagina = strtol(pch, NULL, 10);

				mensaje = enviarOrdenAMemoria(pid, 0, pagina, "/", idNodo);
			}
			else
			{
				if (strncmp(pch, "leer", 5) == 0)
				{
					pch = strtok(NULL, " \n");
					pagina = strtol(pch, NULL, 10);

					mensaje = enviarOrdenAMemoria(pid, 1, pagina, "/", idNodo);
				}
				else
				{
					pch = strtok(NULL, " \n");
					pagina = strtol(pch, NULL, 10);

					pch = strtok(NULL, " \n");

					mensaje = enviarOrdenAMemoria(pid, 2, pagina, pch, idNodo);
				}//else escritura
			}//else escritura/lectura

			if(mensaje.orden!=1)
			{
				list_add(listaRespuestas, respuesta_create(mensaje.pid, mensaje.orden, mensaje.pagina, mensaje.content));
			}

		}//else iniciar
	}//else entrada-salida

	sleep(RETARDO);
	enviarRespuestaPlanificador(socketPlanificador, mensaje.pid, mensaje.orden, mensaje.pagina, mensaje.content);

	if((mensaje.orden==3) || (mensaje.orden ==5) )
	{
		log_info(logger, "Rafaga concluida. PID %d.", mensaje.pid);

		enviarRespuestas(socketPlanificador, mensaje.pid);
	}
}

char * obtenerLinea(char path[PACKAGESIZE], int puntero) {
	FILE* file;
	file = txt_open_for_read(path);
	char * linea = read_line(file, puntero - 1);

	txt_close_file(file);

	return linea;
}

void removerPID(int pid)
{
	t_orden_CPU *new;
	new = buscarPID(pid);

	while(new!=NULL)
	{
		bool compararPorIdentificador2(t_orden_CPU *unaCaja)
		{
			if (unaCaja->pid == pid)
			{
				return 1;
			}

			return 0;
		}

		list_remove_and_destroy_by_condition(listaRespuestas, (void*) compararPorIdentificador2, (void*) respuesta_destroy);

		new = buscarPID(pid);
	}

}

t_orden_CPU* buscarPID(int pid)
{
	bool compararPorIdentificador2(t_orden_CPU *unaCaja)
	{
		if (unaCaja->pid == pid)
		{
			return 1;
		}

		return 0;
	}
	return list_find(listaRespuestas, (void*) compararPorIdentificador2);
}

t_cpu* buscarCPU(int sock)
{
	bool compararPorIdentificador2(t_cpu *unaCaja)
	{
		if (unaCaja->socketPan == sock)
		{
			return 1;
		}

		return 0;
	}
	return list_find(listaCPUs, (void*) compararPorIdentificador2);
}

int encontrarPosicionCPU(int sock)
{
	t_cpu* new;

	int i=0;
	int encontrado = 1;

	while( (i<list_size(listaCPUs)) && encontrado!=0)
	{
		new = list_get(listaCPUs,i);

		if(new->socketPan == sock)
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
static t_cpu *cpu_create(int idCPU, int cantInst, int socketPlan)
{
	t_cpu *new = malloc(sizeof(t_cpu));

	 new->idCPU=idCPU;
	 new->cantInst=cantInst;
	 new->socketPan = socketPlan;

	 return new;
}
static void cpu_destroy(t_cpu *self)
{
    free(self);
}
static t_orden_CPU *respuesta_create(int pid, int orden, int pagina, char content[PACKAGESIZE])
{
	t_orden_CPU *new = malloc(sizeof(t_orden_CPU));

	 new->pid = pid;
	 new->orden = orden;
	 new->pagina = pagina;
	 strcpy(new->content, content);
	 new->contentSize = strlen(content);

	 return new;
}
static void respuesta_destroy(t_orden_CPU *self)
{
    free(self);
}
