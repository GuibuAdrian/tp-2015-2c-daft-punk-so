/*
 * servidorM.c
 *
 *  Created on: 12/10/2015
 *      Author: utnso
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>

#include <commons/collections/list.h>
#include <commons/collections/queue.h>
#include "socket.h"
#include <commons/config.h>
#include <commons/log.h>

#define BACKLOG 5			// Define cuantas conexiones vamos a mantener pendientes al mismo tiempo
#define PACKAGESIZE 1024	// Define cual va a ser el size maximo del paquete a enviar
#define INICIAR 0
#define LEER 1
#define ESCRIBIR 2
#define FINALIZAR 3

//a cada PID se le asocia una tabla de paginas luego se puede hacer lecturas o modificaciones
typedef struct {
	int pagina;
	int marco;
	int bitReferencia;  //Funcion: para LRU para saber la ultima ref || para CLOCK para saber BIT de USO
	int bitModificado;
	char puntero;
} t_tablaPags;

typedef struct {
	int pid;
	int paginas;
	t_list *tablaDePaginas;
} t_tablaDeProcesos;

typedef struct {
	int pid;
	int fallos;
	int accedidas;
} t_fallosPid;

typedef struct {
	int pid;
	int pagina;
	int marco;
} t_TLB;

typedef struct {
	int pid;
	int orden;	// 0=Iniciar, 1=Leer, 2=Escribir, 3=Finalizar
	int pagina;
	int contentSize;
	char content[PACKAGESIZE];
} t_orden_CPU;

t_log* logger;
t_list *tablaDeProcesos, *fallosPid, *listaTLB;
pthread_mutex_t mutexTLB, mutexMemoFlush;
char *TLBHabil, *politicaDeReemplazo;
int socketSwap, maxMarcos, cantMarcos, tamMarcos, entradasTLB, retardoMem;
float totalAccesos, totalTLBHits;
void* memoriaPrincipal;

static t_tablaDeProcesos *tablaProc_create(int pid, int pagina);
static void tablaProc_destroy(t_tablaDeProcesos *self);
static t_fallosPid *fallos_create(int pid, int fallos, int accesos);
static void fallos_destroy(t_fallosPid *self);
static t_tablaPags *tablaPag_create(int pagina, int marco, int referencia, int modificado, char puntero);
static void tablaPag_destroy(t_tablaPags *self);
static t_TLB *TLB_create(int pid, int pagina, int marco);
static void TLB_destroy(t_TLB *self);
int tamanioOrdenCPU(t_orden_CPU mensaje);
int tamanioOrdenCPU1(t_orden_CPU mensaje);

t_tablaPags* buscarPagEnMemoriaPpal(int pid, int pagina);
t_tablaPags* buscarPagEnTablaDePags(int pagina, t_tablaDeProcesos* new);
t_tablaDeProcesos* buscarPID(int pid);
t_fallosPid* buscarPIDFallos(int pid);
t_TLB* removerPIDEntrada(int pid);
t_TLB* buscarPagEnTLB(int pid, int pag);
int buscarMarcoEnTablaDePags(t_tablaDeProcesos* new, int marcoBuscado);
int buscarRefMaxima(t_list *tablaDePaginas);
int encontrarPosicionEnTLB(int pid, int pagina);
int encontrarPosicionEnProcesos(int pid);
int encontrarPosicionEnFallos(int pid);
int encontrarPosicionEnTablaDePags(int pag, t_list *listaTablaPags);
int encontrarPosReferencia(t_list *listaTablaPags, int ref);
int encontrarPosUsoYModificado_cero(t_list *listaTablaPags);
int encontrarPosUso_cero_YModificado_uno(t_list *listaTablaPags);
int encontrarPosPuntero(t_list *tablaDePaginas);
void ordenarPorMarco(t_list *listaTablaPags);

void recibirConexiones1(char * PUERTO_CPU);
t_orden_CPU enviarOrdenASwap(int pid, int orden, int paginas, char *content);
void enviarRespuestaCPU(t_orden_CPU respuestaMemoria, int socketCPU);
void procesarOrden(t_orden_CPU mensaje, int socketCPU);
void operarConTLB( t_TLB* entradaTLB, t_orden_CPU mensaje, int socketCPU);
void tlbHistorica();
void iniciarProceso(int pid, int paginas);
void finalizarProceso(int pid);
void escribirPagina(int marco, char* content, int contentSize);
void rutinaFlushTLB();
void rutinaLimpiarMemoriaPrincipal();
void dumpMemoriaPrincipal();
void borrarPIDEnTLB(int pid);
void aumentarBitReferencia(t_list* new);
void cambiarBitReferencia(int pid, int pagina);
void cambiarBitUsoYModificado(int pid, int pagina, int marco, int uso, int mod);
void mostrarTablaDePags(int pid);
void mostrarTLB();
void mostrarMemoriaPpal();
void aumentarFallos_Swap(int pid, int cantFallo, int cantSwap);

int main() {
	printf("\n");
	printf("~~~~~~~~~~MEMORIA~~~~~~~~~~\n\n");

	signal(SIGUSR1, rutinaFlushTLB);
	signal(SIGUSR2, rutinaLimpiarMemoriaPrincipal);
	signal(SIGPOLL, dumpMemoriaPrincipal);

	logger = log_create("logsTP", "Memoria", 1, LOG_LEVEL_INFO);

	t_config* config;

	config = config_create("admin_memoria.conf");

	char *IP = config_get_string_value(config, "IP_SWAP");
	char * PUERTO_SWAP = config_get_string_value(config, "PUERTO_SWAP");
	maxMarcos = config_get_int_value(config, "MAXIMO_MARCOS_POR_PROCESO");
	cantMarcos = config_get_int_value(config, "CANTIDAD_MARCOS");
	tamMarcos = config_get_int_value(config, "TAMANIO_MARCO");
	entradasTLB = config_get_int_value(config, "ENTRADAS_TLB");
	retardoMem = config_get_int_value(config, "RETARDO_MEMORIA");
	TLBHabil = config_get_string_value(config, "TLB_HABILITADA");
	politicaDeReemplazo = config_get_string_value(config, "POLITICA_DE_REEMPLAZO");


	pthread_mutex_init(&mutexTLB, NULL);
	pthread_mutex_init(&mutexMemoFlush, NULL);

	totalAccesos = 0, totalTLBHits = 0;

	socketSwap = conectarse(IP, PUERTO_SWAP);
	log_info(logger, "Conectado a Swap");

	char * PUERTO_CPU = config_get_string_value(config, "PUERTO_CPU");

	memoriaPrincipal = malloc(cantMarcos * tamMarcos);
	listaTLB = list_create();

	tablaDeProcesos = list_create();
	fallosPid = list_create();

	pthread_t unHilo;
	pthread_create(&unHilo,NULL,(void*) tlbHistorica, NULL);

	recibirConexiones1(PUERTO_CPU);

	totalTLBHits=-1;

	list_destroy_and_destroy_elements(tablaDeProcesos,
			(void*) tablaProc_destroy);
	close(socketSwap);
	free(memoriaPrincipal);

	config_destroy(config);
	log_info(logger, "---------------------FIN---------------------");
	log_destroy(logger);

	return 0;
}

void recibirConexiones1(char * PUERTO_CPU) {
	fd_set readset, tempset;
	int maxfd;
	int socketCPU, j, result;

	t_orden_CPU mensaje;
	void* package = malloc(tamanioOrdenCPU1(mensaje));

	int listenningSocket = recibirLlamada(PUERTO_CPU);

	FD_ZERO(&readset);
	FD_SET(listenningSocket, &readset);
	maxfd = listenningSocket;

	do {
		memcpy(&tempset, &readset, sizeof(tempset));
		result = select(maxfd + 1, &tempset, NULL, NULL, NULL);

		if (result < 0 && errno != EINTR) {
			log_error(logger, "Error in select(): %s\n", strerror(errno));
		} else if (result > 0) {
			if (FD_ISSET(listenningSocket, &tempset)) {
				socketCPU = aceptarLlamada(listenningSocket);

				log_info(logger, "Conectado al CPU, urra!");

				if (socketCPU < 0) {
					log_error(logger, "Error in accept(): %s\n",
							strerror(errno));
				} else {
					FD_SET(socketCPU, &readset);
					maxfd = (maxfd < socketCPU) ? socketCPU : maxfd;
				}

				FD_CLR(listenningSocket, &tempset);
			}
			for (j = 0; j < maxfd + 1; j++) {
				if (FD_ISSET(j, &tempset)) {
					result = recv(socketCPU, (void*) package,
							sizeof(mensaje.pid), 0);

					if (result > 0) {	// aqui se reciben las ordenes?
						memcpy(&mensaje.pid, package, sizeof(mensaje.pid));

						recv(socketCPU, (void*) (package + sizeof(mensaje.pid)),
								sizeof(mensaje.pagina), 0);
						memcpy(&mensaje.orden, package + sizeof(mensaje.pid),
								sizeof(mensaje.orden));

						recv(socketCPU,
								(void*) (package + sizeof(mensaje.pid)
										+ sizeof(mensaje.orden)),
								sizeof(mensaje.pagina), 0);
						memcpy(&mensaje.pagina,
								package + sizeof(mensaje.pid)
										+ sizeof(mensaje.orden),
								sizeof(mensaje.pagina));
						recv(socketCPU,
								(void*) (package + sizeof(mensaje.pid)
										+ sizeof(mensaje.orden)
										+ sizeof(mensaje.pagina)),
								sizeof(mensaje.contentSize), 0);
						memcpy(&mensaje.contentSize,
								package + sizeof(mensaje.pid)
										+ sizeof(mensaje.orden)
										+ sizeof(mensaje.pagina),
								sizeof(mensaje.contentSize));

						void* package2 = malloc(mensaje.contentSize);

						recv(socketCPU, (void*) package2, mensaje.contentSize,
								0);	//campo longitud(NO SIZEOF DE LONGITUD)
						memcpy(&mensaje.content, package2, mensaje.contentSize);


						int pid = mensaje.pid;

						if (strncmp(TLBHabil, "NO", 2) == 0)
						{
							procesarOrden(mensaje, socketCPU);
						}
						else
						{
							pthread_mutex_lock(&mutexTLB);

							t_TLB* new = buscarPagEnTLB(pid, mensaje.pagina);

							pthread_mutex_unlock(&mutexTLB);

							if ( (new != NULL) && (mensaje.orden != 0) && (mensaje.orden != 3) )  //Esta en la TLB
							{
								totalTLBHits++;
								totalAccesos++;

								operarConTLB(new, mensaje, socketCPU);
							}
							else
							{

								procesarOrden(mensaje, socketCPU);
							}
						}
						mostrarTablaDePags(mensaje.pid);


						free(package2);
					} else if (result == 0) {
						close(j);
						FD_CLR(j, &readset);
					} else {
						log_error(logger, "Error in recv(): %s\n",
								strerror(errno));
					}
				}      // end if (FD_ISSET(j, &tempset))
			}      // end for (j=0;...)
			if (result == 0) {
				break;
			}
		}      // end else if (result > 0)
	} while (1);

	free(package);

	close(socketCPU);
	close(listenningSocket);
}

void actualizarTLB(int pid, int pag, int marco)
{
	if(list_size(listaTLB)==entradasTLB)
	{
		list_remove_and_destroy_element(listaTLB, 0, (void*)TLB_destroy);
	}

	list_add(listaTLB, TLB_create(pid, pag, marco));
}

t_tablaPags* clockMejorado(t_list* listaTablaPags, int pag, int pid, int orden)
{
	ordenarPorMarco(listaTablaPags);

	t_tablaPags* new2;
	int posEncontrado = -1;
	int modificado = 0;
	int uso = 1;
	int marco;

	while(posEncontrado == -1)
	{
		posEncontrado = encontrarPosUsoYModificado_cero(listaTablaPags);

		if(posEncontrado == -1)
		{
			posEncontrado = encontrarPosUso_cero_YModificado_uno(listaTablaPags);
		}
	}

	new2 = list_remove(listaTablaPags, posEncontrado);
	marco = new2->marco;

	if(orden==ESCRIBIR)
	{
		modificado = 1;
	}

	list_add_in_index(listaTablaPags, posEncontrado, tablaPag_create(pag, marco, uso, modificado, 0));

	return new2;
}
t_tablaPags* lru(t_list *listaTablaPags, int pag, int pid, int orden)
{
	t_tablaPags* new2;

	aumentarBitReferencia(listaTablaPags);

	int refMax = buscarRefMaxima(listaTablaPags);

	int posRemove = encontrarPosReferencia(listaTablaPags, refMax);

	new2 = list_remove(listaTablaPags, posRemove);

	int modificado = 0;

	if(orden==ESCRIBIR)
	{
		modificado = 1;
	}

	list_add(listaTablaPags, tablaPag_create(pag, new2->marco, 0, modificado, -1));

	return new2;
}

t_tablaPags* fifo(t_list *listaTablaPags, int pag, int orden)
{
	t_tablaPags* new2;

	new2 = list_remove(listaTablaPags, 0);

	int modificado = 0;

	if(orden==ESCRIBIR)
	{
		modificado = 1;
	}

	list_add(listaTablaPags, tablaPag_create(pag, new2->marco, -1, modificado,-1));

	return new2;
}

t_tablaPags* aplicarAlgoritmo(t_tablaDeProcesos* new, int pag, int pid, int orden)
{
	t_tablaPags* new2;

	if(strncmp(politicaDeReemplazo, "FIFO",4)==0)
	{
		log_info(logger, "FIFO");

		new2 = fifo(new->tablaDePaginas, pag, orden);
	}
	else
	{
		if(strncmp(politicaDeReemplazo, "LRU",3)==0)
		{
			log_info(logger, "LRU");

			new2 = lru(new->tablaDePaginas, pag, pid, orden);
		}
		else
		{
			log_info(logger, "CLOCK MEJORADO");

			new2 = clockMejorado(new->tablaDePaginas, pag, pid, orden);
		}
	}

	return new2;
}

int reemplazarPagina(t_tablaDeProcesos* new, int pag, int orden)
{
	int marco, pagina, pid;

	pid = new->pid;

	t_tablaPags* new2;

	new2 = aplicarAlgoritmo(new, pag, new->pid, orden);  //Devuelvo la pag q reemplazo

	log_info(logger, "Reemplazando pag: %d, por pag: %d en el marco: %d", new2->pagina, pag, new2->marco);

	t_TLB* entradaTLB = buscarPagEnTLB(new->pid, new2->pagina);

	if(entradaTLB!=NULL) //Si la pag q reemplazo en Memoria Princial esta en la TLB
	{
		int posTLB = encontrarPosicionEnTLB(new->pid, new2->pagina);

		list_remove_and_destroy_element(listaTLB, posTLB, (void*) TLB_destroy);
	}

	marco = new2->marco;
	pagina = new2->pagina;

	if(new2->bitModificado==1)
	{
		log_info(logger, "Solicitando a SWAP mProc: %d Pag: %d", pid, pagina);

		enviarOrdenASwap(new->pid, ESCRIBIR, pagina, memoriaPrincipal+marco*tamMarcos);
		aumentarFallos_Swap(new->pid, 0, 1);
	}


	tablaPag_destroy(new2);

	return marco;
}

int asignarMarco()
{
	int i = 0, encontrado = -1, marcoBuscado = 0;
	t_tablaDeProcesos* new;

	while ((i < list_size(tablaDeProcesos)) && (encontrado == -1) && (marcoBuscado < cantMarcos))
	{
		while ((i < list_size(tablaDeProcesos)) && (encontrado == -1))
		{
			new = list_get(tablaDeProcesos, i);

			encontrado = buscarMarcoEnTablaDePags(new, marcoBuscado);

			i++;
		}

		if (encontrado == 1)
		{
			i = 0;
			marcoBuscado++;
			encontrado = -1;
		}
		else
		{
			break;
		}
	}

	if ((marcoBuscado >= cantMarcos))
	{
		return -1;
	}
	else
	{
		if ((i >= list_size(tablaDeProcesos)) && (encontrado == -1))
		{
			return marcoBuscado;
		}
		else
		{
			return -1;
		}
	}
}

int actualizarMemoriaPpal(t_tablaDeProcesos* new, int pag, int orden)
{
	int totalPag = list_size(new->tablaDePaginas);
	int marco;

	if (totalPag == maxMarcos) //Si la cantidad de marcos ocupados es MAX entonces empiezo a reemplazar
	{
		log_info(logger, "Inicial");
		mostrarTablaDePags(new->pid);

		marco = reemplazarPagina(new, pag, orden);

		log_info(logger, "Final");
		mostrarTablaDePags(new->pid);
	}
	else
	{
		marco = asignarMarco(); //Recorro la lista de procesos hasta encontrar algun marco valido (algun marco q no este en la tabla de paginas)

		if (marco == -1)//-1 No puedo asignarle marcos
		{
			if(!list_is_empty(new->tablaDePaginas)) // Si tiene al menos 1 una pag en memoria la reemplazo
			{
				log_info(logger, "Inicial");
				mostrarTablaDePags(new->pid);

				marco = reemplazarPagina(new, pag, orden);

				log_info(logger, "Final");
				mostrarTablaDePags(new->pid);
			}
			else
			{
				return marco;
			}
		}
		else //Si no lo agrego
		{
			int modificado = 0;

			if(orden == ESCRIBIR)
			{
				modificado = 1;
			}

			if(strncmp(politicaDeReemplazo, "FIFO", 4)==0)
			{
				list_add(new->tablaDePaginas, tablaPag_create(pag, marco, -1, modificado,-1));

			}
			else
			{
				if(strncmp(politicaDeReemplazo, "LRU",3)==0)
				{
					aumentarBitReferencia(new->tablaDePaginas);

					list_add(new->tablaDePaginas, tablaPag_create(pag, marco, 0, modificado,-1));
				}
				else
				{
					int puntero = 0;

					if(list_is_empty(new->tablaDePaginas))
					{
						puntero = 1;
					}

					list_add(new->tablaDePaginas, tablaPag_create(pag, marco, 1, modificado, puntero));
				}
			}
		}
	}

	pthread_mutex_lock(&mutexTLB);
	actualizarTLB(new->pid, pag, marco);
	pthread_mutex_unlock(&mutexTLB);

	return marco;
}

void procesarOrden(t_orden_CPU mensaje, int socketCPU) {
	t_tablaDeProcesos* new;
	t_tablaPags* new2;
	t_orden_CPU respuestaSwap;

	if (mensaje.orden == INICIAR)
	{
		iniciarProceso(mensaje.pid, mensaje.pagina);

		respuestaSwap = enviarOrdenASwap(mensaje.pid, mensaje.orden, mensaje.pagina, mensaje.content);

		log_info(logger, "Proceso %d iniciado de %d pags", mensaje.pid, mensaje.pagina);

		enviarRespuestaCPU(respuestaSwap, socketCPU);
	}
	else
		if (mensaje.orden == FINALIZAR)
		{
			finalizarProceso(mensaje.pid);//Borro al pid de la TLB y la tabla de pags

			respuestaSwap = enviarOrdenASwap(mensaje.pid, mensaje.orden, mensaje.pagina, mensaje.content);

			enviarRespuestaCPU(respuestaSwap, socketCPU);
		}
		else
		{
			log_info(logger, "No esta en TLB");

			pthread_mutex_lock(&mutexMemoFlush);

			new2 = buscarPagEnMemoriaPpal(mensaje.pid, mensaje.pagina);

			pthread_mutex_unlock(&mutexMemoFlush);

			if (new2 != NULL) //Si esta en Memoria Principal
			{
				int marco = new2->marco;

				log_info(logger, "Proc: %d, Pag: %d. Esta en memoria principal, marco: %d", mensaje.pid, mensaje.pagina, marco);

				pthread_mutex_lock(&mutexTLB);

				if(strncmp(politicaDeReemplazo, "LRU",3)==0)
				{
					cambiarBitReferencia(mensaje.pid, mensaje.pagina);
				}

				totalAccesos++;

				if (mensaje.orden == LEER)	// leer pagina de un proceso
				{
					if(strncmp(politicaDeReemplazo, "CLOCK",5)==0)
					{
						cambiarBitUsoYModificado(mensaje.pid, mensaje.pagina, marco, 1, -1);
					}

					respuestaSwap.pid = mensaje.pid;
					respuestaSwap.pagina = mensaje.pagina;
					respuestaSwap.orden = 2;

					log_info(logger, "Proceso %d Leyendo pag: %d.", respuestaSwap.pid, respuestaSwap.pagina);

					pthread_mutex_lock(&mutexMemoFlush);

					strncpy(respuestaSwap.content, memoriaPrincipal + marco * tamMarcos, strlen(memoriaPrincipal + marco * tamMarcos)+1);

					pthread_mutex_unlock(&mutexMemoFlush);

					 usleep(retardoMem*1000000);

					respuestaSwap.contentSize = strlen(respuestaSwap.content) + 1;

					enviarRespuestaCPU(mensaje, socketCPU);
				}
				else
					if (mensaje.orden == ESCRIBIR)	// escribe pagina de un proceso
					{
						log_info(logger, "Proceso %d Escribiendo en pag: %d", mensaje.pid, mensaje.pagina);

						if(strncmp(politicaDeReemplazo, "CLOCK",3)==0)
						{
							cambiarBitUsoYModificado(mensaje.pid, mensaje.pagina, marco, 1, 1);
						}
						else
						{
							cambiarBitUsoYModificado(mensaje.pid, mensaje.pagina, marco, -1, 1);
						}

						pthread_mutex_unlock(&mutexMemoFlush);

						escribirPagina(marco, mensaje.content, mensaje.contentSize);

						pthread_mutex_unlock(&mutexMemoFlush);

						usleep(retardoMem*1000000);

						enviarRespuestaCPU(mensaje, socketCPU); //Le devuelvo el contenido del marco al CPU
					}

				actualizarTLB(respuestaSwap.pid, respuestaSwap.pagina, marco);

				pthread_mutex_unlock(&mutexTLB);
			}
			else  //No esta en Memoria Principal. La traigo desde SWAP
			{
				new = buscarPID(mensaje.pid);

				pthread_mutex_lock(&mutexMemoFlush);

				int marco = actualizarMemoriaPpal(new, mensaje.pagina, mensaje.orden);

				pthread_mutex_unlock(&mutexMemoFlush);


				if (marco == -1) // Si no se le puede asignar mas marcos FALLO!!!!
				{
					mensaje.orden = 1;

					log_info(logger, "mProc: %d, FALLO", mensaje.pid);

					enviarRespuestaCPU(mensaje, socketCPU);
				}
				else
				{
					aumentarFallos_Swap(mensaje.pid, 1, 1);

					totalAccesos++;

					if (mensaje.orden == LEER)	// leer pagina de un proceso
					{
						log_info(logger, "Proceso %d Leyendo pag: %d", mensaje.pid, mensaje.pagina);

						log_info(logger, "Solicitando a SWAP mProc: %d Pag: %d", mensaje.pid, mensaje.pagina);

						respuestaSwap = enviarOrdenASwap(mensaje.pid, mensaje.orden, mensaje.pagina, mensaje.content);

						pthread_mutex_lock(&mutexMemoFlush);

						strncpy(respuestaSwap.content, memoriaPrincipal + marco * tamMarcos, strlen(memoriaPrincipal + marco * tamMarcos)+1);

						pthread_mutex_unlock(&mutexMemoFlush);

						respuestaSwap.contentSize = strlen(respuestaSwap.content) + 1;

						 usleep(retardoMem*1000000);

						enviarRespuestaCPU(respuestaSwap, socketCPU);
					}
					else
					{
						if (mensaje.orden == ESCRIBIR)  // escribe pagina de un proceso
						{
							log_info(logger, "Proceso %d Escribiendo en pag: %d", mensaje.pid, mensaje.pagina);

							if(mensaje.contentSize > tamMarcos) {
								log_error(logger, "Recibí un mensaje para escribir en %d que tiene %d bytes y el tamanio de marco es %d", mensaje.pid, mensaje.contentSize, tamMarcos);
								return;
							}

							log_info(logger, "Solicitando a SWAP mProc: %d Pag: %d", mensaje.pid, mensaje.pagina);

							respuestaSwap = enviarOrdenASwap(mensaje.pid, LEER, mensaje.pagina, mensaje.content); //Le aviso al SWAP del nuevo contenido

							pthread_mutex_lock(&mutexMemoFlush);

							escribirPagina(marco, mensaje.content, mensaje.contentSize);

							pthread_mutex_unlock(&mutexMemoFlush);

							usleep(retardoMem*1000000);

							strncpy(respuestaSwap.content, mensaje.content, strlen(mensaje.content));

							enviarRespuestaCPU(respuestaSwap, socketCPU);
						}
					} //else escribir
			} //else fallo asignando marco
		} //else no esta en memoria
	} //else orden de incio/fin
}

void operarConTLB( t_TLB* entradaTLB, t_orden_CPU mensaje, int socketCPU)
{

	t_orden_CPU respuestaSwap;

	respuestaSwap.pid = entradaTLB->pid;
	respuestaSwap.pagina = entradaTLB->pagina;
	int marco = entradaTLB->marco;

	log_info(logger, "Esta en TLB. Proc: %d, Pag: %d, Marco: %d", respuestaSwap.pid, mensaje.pagina, marco);

	if(strncmp(politicaDeReemplazo, "LRU",3)==0)
	{
		cambiarBitReferencia(mensaje.pid, mensaje.pagina);
	}

	if (mensaje.orden == LEER)// leer pagina de un proceso
	{
		respuestaSwap.orden = 2;

		log_info(logger, "Proceso %d Leyendo pag: %d.", mensaje.pid, mensaje.pagina);

		pthread_mutex_lock(&mutexMemoFlush);

		strncpy(respuestaSwap.content, memoriaPrincipal + marco * tamMarcos, strlen(memoriaPrincipal + marco * tamMarcos)+1);

		pthread_mutex_unlock(&mutexMemoFlush);

		respuestaSwap.contentSize = strlen(respuestaSwap.content) + 1;

		enviarRespuestaCPU(respuestaSwap, socketCPU);//Le devuelvo el contenido del marco al CPU
	}
	else if (mensaje.orden == ESCRIBIR)// escribe pagina de un proceso
	{
		if(strncmp(politicaDeReemplazo, "CLOCK",3)==0)
		{
			cambiarBitUsoYModificado(mensaje.pid, mensaje.pagina, marco, 1, 1);
		}
		else
		{
			cambiarBitUsoYModificado(mensaje.pid, mensaje.pagina, marco, -1, 1);
		}

		log_info(logger, "Proceso %d Escribiendo en pag: %d", respuestaSwap.pid, respuestaSwap.pagina);

		respuestaSwap.orden = 4;

		pthread_mutex_lock(&mutexMemoFlush);

		escribirPagina(marco, mensaje.content, mensaje.contentSize);

		respuestaSwap.contentSize = strlen(mensaje.content);
		strncpy(respuestaSwap.content, mensaje.content, respuestaSwap.contentSize);


		pthread_mutex_unlock(&mutexMemoFlush);

		enviarRespuestaCPU(respuestaSwap, socketCPU); //Le devuelvo el contenido del marco al CPU
	}
}

void iniciarProceso(int pid, int paginas)
{
	list_add(tablaDeProcesos, tablaProc_create(pid, paginas));
	list_add(fallosPid, fallos_create(pid, 0, 0));
}

void finalizarProceso(int pid)
{
	int i;
	t_tablaDeProcesos* new;
	t_tablaPags *new2;
	t_fallosPid* new4;

	int posProc = encontrarPosicionEnProcesos(pid);
	int posFallo = encontrarPosicionEnFallos(pid);

	new = list_remove(tablaDeProcesos, posProc);
	new4 = list_remove(fallosPid, posFallo);

	for(i=0; i<list_size(new->tablaDePaginas);i++)
	{
		new2 = list_remove(new->tablaDePaginas, i);

		t_TLB* new3 = buscarPagEnTLB(pid, new2->pagina);

		if(new3!=NULL)
		{
			int posTLB = encontrarPosicionEnTLB(pid, new2->pagina);

			list_remove_and_destroy_element(listaTLB, posTLB, (void*) TLB_destroy);

		}

		tablaPag_destroy(new2);
	}

	log_info(logger, "Proceso %d Finalizado. Fallos: %d | Accesos: %d", new->pid, new4->fallos, new4->accedidas);

	fallos_destroy(new4);
	tablaProc_destroy(new);

	borrarPIDEnTLB(pid);
}

void aumentarFallos_Swap(int pid, int cantFallo, int cantSwap)
{
	t_fallosPid *new4;
	new4 = buscarPIDFallos(pid);
	int posFallos = encontrarPosicionEnFallos(new4->pid);

	list_replace_and_destroy_element(fallosPid, posFallos, fallos_create(new4->pid, new4->fallos+cantFallo, new4->accedidas+cantSwap), (void*) fallos_destroy);

}
void escribirPagina(int marco, char* content, int contentSize)
{
	strncpy(memoriaPrincipal + marco * tamMarcos, content, contentSize); //Actualizo la memo ppal
}

void cambiarBitUsoYModificado(int pid, int pagina, int marco, int uso, int mod)
{
	t_tablaDeProcesos* new;
	t_tablaPags* new2;

	new = buscarPID(pid);
	new2 = buscarPagEnTablaDePags(pagina, new);

	int puntero = new2->puntero;
	int modificado = new2->bitModificado;
	int usado = new2->bitReferencia;

	int posPag = encontrarPosicionEnTablaDePags(pagina, new->tablaDePaginas);

	if(uso == -1)
	{
		list_replace_and_destroy_element(new->tablaDePaginas, posPag, tablaPag_create(pagina, marco, usado, mod, puntero), (void*) tablaPag_destroy);
	}
	else
	{
		if(mod == -1)
		{
			list_replace_and_destroy_element(new->tablaDePaginas, posPag, tablaPag_create(pagina, marco, uso, modificado, puntero), (void*) tablaPag_destroy);

		}
		else
		{
			list_replace_and_destroy_element(new->tablaDePaginas, posPag, tablaPag_create(pagina, marco, uso, mod, puntero), (void*) tablaPag_destroy);
		}
	}
}

void aumentarBitReferencia(t_list* new)
{
	int i;
	t_tablaPags* new2;

	for(i=0; i<list_size(new); i++)
	{
		new2 = list_get(new, i);

		list_replace_and_destroy_element(new, i, tablaPag_create(new2->pagina, new2->marco, new2->bitReferencia+1, new2->bitModificado, -1), (void*) tablaPag_destroy);
	}
}

void cambiarBitReferencia(int pid, int pagina)
{
	t_tablaDeProcesos* new = buscarPID(pid);
	t_tablaPags* new2;

	aumentarBitReferencia(new->tablaDePaginas);

	int posPag = encontrarPosicionEnTablaDePags(pagina, new->tablaDePaginas);
	new2 = list_get(new->tablaDePaginas, posPag);

	list_replace_and_destroy_element(new->tablaDePaginas, posPag, tablaPag_create(new2->pagina, new2->marco, 0, new2->bitModificado, -1), (void*) tablaPag_destroy);
}

void enviarRespuestaCPU(t_orden_CPU respuestaMemoria, int socketCPU) {
	t_orden_CPU mensajeSwap;

	mensajeSwap.pid = respuestaMemoria.pid;
	mensajeSwap.orden = respuestaMemoria.orden;
	mensajeSwap.pagina = respuestaMemoria.pagina;
	mensajeSwap.contentSize = respuestaMemoria.contentSize;

	void* mensajeSwapPackage = malloc(tamanioOrdenCPU(mensajeSwap));

	memcpy(mensajeSwapPackage, &mensajeSwap.pid, sizeof(mensajeSwap.pid));
	memcpy(mensajeSwapPackage + sizeof(mensajeSwap.pid), &mensajeSwap.orden,
			sizeof(mensajeSwap.orden));
	memcpy(
			mensajeSwapPackage + sizeof(mensajeSwap.pid)
					+ sizeof(mensajeSwap.orden), &mensajeSwap.pagina,
			sizeof(mensajeSwap.pagina));
	memcpy(
			mensajeSwapPackage + sizeof(mensajeSwap.pid)
					+ sizeof(mensajeSwap.orden) + sizeof(mensajeSwap.pagina),
			&mensajeSwap.contentSize, sizeof(mensajeSwap.contentSize));
	memcpy(
			mensajeSwapPackage + sizeof(mensajeSwap.pid)
					+ sizeof(mensajeSwap.orden) + sizeof(mensajeSwap.pagina)
					+ sizeof(mensajeSwap.contentSize), &respuestaMemoria.content,
			mensajeSwap.contentSize);

	send(socketCPU, mensajeSwapPackage, tamanioOrdenCPU(mensajeSwap), 0);

	free(mensajeSwapPackage);
}

t_orden_CPU recibirRespuestaSwap(int socketMemoria) {
	t_orden_CPU mensajeSwap;

	void* package = malloc(
			sizeof(mensajeSwap.pid) + sizeof(mensajeSwap.orden)
					+ sizeof(mensajeSwap.pagina)
					+ sizeof(mensajeSwap.contentSize));

	recv(socketMemoria, (void*) package, sizeof(mensajeSwap.pid), 0);
	memcpy(&mensajeSwap.pid, package, sizeof(mensajeSwap.pid));
	recv(socketMemoria, (void*) (package + sizeof(mensajeSwap.pid)),
			sizeof(mensajeSwap.orden), 0);
	memcpy(&mensajeSwap.orden, package + sizeof(mensajeSwap.pid),
			sizeof(mensajeSwap.orden));	//--
	recv(socketMemoria,
			(void*) (package + sizeof(mensajeSwap.pid)
					+ sizeof(mensajeSwap.orden)), sizeof(mensajeSwap.pagina),
			0);
	memcpy(&mensajeSwap.pagina,
			package + sizeof(mensajeSwap.pid) + sizeof(mensajeSwap.orden),
			sizeof(mensajeSwap.pagina));	//--
	recv(socketMemoria,
			(void*) (package + sizeof(mensajeSwap.pid)
					+ sizeof(mensajeSwap.orden) + sizeof(mensajeSwap.pagina)),
			sizeof(mensajeSwap.contentSize), 0);	//--
	memcpy(&mensajeSwap.contentSize,
			(void*) package + sizeof(mensajeSwap.pid)
					+ sizeof(mensajeSwap.orden) + sizeof(mensajeSwap.pagina),
			sizeof(mensajeSwap.contentSize));

	void* package2 = malloc(mensajeSwap.contentSize);

	recv(socketMemoria, (void*) package2, mensajeSwap.contentSize, 0);//campo longitud(NO SIZEOF DE LONGITUD)
	memcpy(&mensajeSwap.content, package2, mensajeSwap.contentSize);

	free(package);
	free(package2);

	return mensajeSwap;
}

t_orden_CPU enviarOrdenASwap(int pid, int orden, int paginas, char *content) {
	t_orden_CPU mensajeSwap;

	mensajeSwap.pid = pid;
	mensajeSwap.orden = orden;
	mensajeSwap.pagina = paginas;
	mensajeSwap.contentSize = strlen(content) + 1;
	strcpy(mensajeSwap.content, content);

	void* mensajeSwapPackage = malloc(
			tamanioOrdenCPU(mensajeSwap) + mensajeSwap.contentSize);

	memcpy(mensajeSwapPackage, &pid, sizeof(mensajeSwap.pid));
	memcpy(mensajeSwapPackage + sizeof(mensajeSwap.pid), &mensajeSwap.orden,
			sizeof(mensajeSwap.orden));
	memcpy(
			mensajeSwapPackage + sizeof(mensajeSwap.pid)
					+ sizeof(mensajeSwap.orden), &paginas,
			sizeof(mensajeSwap.pagina));
	memcpy(
			mensajeSwapPackage + sizeof(mensajeSwap.pid)
					+ sizeof(mensajeSwap.orden) + sizeof(mensajeSwap.pagina),
					&mensajeSwap.contentSize, sizeof(mensajeSwap.contentSize));
	memcpy(
			mensajeSwapPackage + sizeof(mensajeSwap.pid)
					+ sizeof(mensajeSwap.orden) + sizeof(mensajeSwap.pagina)
					+ sizeof(mensajeSwap.contentSize), &content,
			mensajeSwap.contentSize);
	send(socketSwap, mensajeSwapPackage, tamanioOrdenCPU(mensajeSwap), 0);
	free(mensajeSwapPackage);

	return recibirRespuestaSwap(socketSwap);
}

void borrarPIDEnTLB(int pid)
{
	t_TLB *entradaTLB = removerPIDEntrada(pid);

	while(entradaTLB!=NULL)
	{
		TLB_destroy(entradaTLB);
		entradaTLB = removerPIDEntrada(pid);
	}

}

t_tablaPags* buscarPagEnMemoriaPpal(int pid, int pagina) {
	t_tablaDeProcesos* new = buscarPID(pid);
	t_tablaPags* new2;

	new2 = buscarPagEnTablaDePags(pagina, new);

	return new2;
}

t_tablaPags* buscarPagEnTablaDePags(int pagina, t_tablaDeProcesos* new)
{

	bool compararPorIdentificador2(t_tablaPags* unaCaja) {
		if (unaCaja->pagina == pagina) {
			return 1;
		}

		return 0;
	}
	return list_find(new->tablaDePaginas, (void*) compararPorIdentificador2);
}

t_tablaDeProcesos* buscarPID(int pid)
{
	bool compararPorIdentificador2(t_tablaDeProcesos *unaCaja) {
		if (unaCaja->pid == pid) {
			return 1;
		}

		return 0;
	}
	return list_find(tablaDeProcesos, (void*) compararPorIdentificador2);
}

t_fallosPid* buscarPIDFallos(int pid)
{
	bool compararPorIdentificador2(t_fallosPid *unaCaja) {
		if (unaCaja->pid == pid) {
			return 1;
		}

		return 0;
	}
	return list_find(fallosPid, (void*) compararPorIdentificador2);
}

t_TLB* removerPIDEntrada(int pid)
{
	bool compararPorIdentificador2(t_TLB *unaCaja)
	{
		if(unaCaja->pid == pid)
		{
			return 1;
		}

		return 0;
	}
	return list_remove_by_condition(listaTLB, (void*) compararPorIdentificador2);
}

t_TLB* buscarPagEnTLB(int pid, int pag)
{
	bool compararPorIdentificador2(t_TLB *unaCaja)
	{
		if( (unaCaja->pid == pid) && (unaCaja->pagina == pag))
		{
			return 1;
		}

		return 0;
	}
	return list_find(listaTLB, (void*) compararPorIdentificador2);
}

int buscarMarcoEnTablaDePags(t_tablaDeProcesos* new, int marcoBuscado)
{
	int i = 0, encontrado = -1;
	t_tablaPags* new2;

	while (i < list_size(new->tablaDePaginas))
	{
		new2 = list_get(new->tablaDePaginas, i);


		if ((encontrado == -1) && (new2->marco == marcoBuscado))
		{
			encontrado = 1; //Encontre el marco buscado en memoria. Devuelvo 1
		}

		i++;
	}

	return encontrado;
}

int buscarRefMaxima(t_list *tablaDePaginas)
{
	t_tablaPags* new;
	int i;
	int maxRef = 0;

	for(i=0;i<list_size(tablaDePaginas);i++)
	{
		new = list_get(tablaDePaginas, i);

		if(new->bitReferencia>=maxRef)
		{
			maxRef = new->bitReferencia;
		}
	}

	return maxRef;
}

int encontrarPosicionEnTLB(int pid, int pagina)
{
	t_TLB* new;

	int i=0;
	int encontrado = 1;

	while( (i<list_size(listaTLB)) && encontrado!=0)
	{
		new = list_get(listaTLB,i);

		if( (new->pid == pid) && (new->pagina==pagina) )
		{
			encontrado = 0;
		}
		else
		{
			i++;
		}
	}

	if(encontrado)
	{
		return -1;
	}
	return i;
}

int encontrarPosPuntero(t_list *tablaDePaginas)
{
	t_tablaPags *new;

	int i=0;
	int encontrado = 1;

	while( (i<list_size(tablaDePaginas)) && encontrado!=0)
	{
		new = list_get(tablaDePaginas,i);

		if(new->puntero == 1)
		{
			encontrado = 0;
		}
		else
		{
			i++;
		}
	}

	if(encontrado)
	{
		return -1;
	}
	return i;
}


int encontrarPosicionEnProcesos(int pid)
{
	t_tablaDeProcesos* new;

	int i=0;
	int encontrado = 1;

	while( (i<list_size(tablaDeProcesos)) && encontrado!=0)
	{
		new = list_get(tablaDeProcesos,i);

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

int encontrarPosicionEnFallos(int pid)
{
	t_fallosPid* new;

	int i=0;
	int encontrado = 1;

	while( (i<list_size(fallosPid)) && encontrado!=0)
	{
		new = list_get(fallosPid,i);

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

int encontrarPosicionEnTablaDePags(int pag, t_list *listaTablaPags)
{
	t_tablaPags *new;

	int i=0;
	int encontrado = 1;

	while( (i<list_size(listaTablaPags)) && encontrado!=0)
	{
		new = list_get(listaTablaPags,i);

		if(new->pagina == pag)
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

int encontrarPosReferencia(t_list *listaTablaPags, int ref)
{
	t_tablaPags *new;

	int i=0;
	int encontrado = 1;

	while( (i<list_size(listaTablaPags)) && encontrado!=0)
	{
		new = list_get(listaTablaPags,i);

		if(new->bitReferencia == ref)
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

void moverPuntero(t_list *listaTablaPags, int posPuntero)
{
	posPuntero++;
	t_tablaPags *new = list_get(listaTablaPags, posPuntero);

	if(new==NULL)
	{
		new = list_get(listaTablaPags, 0);

		posPuntero = 0;
	}

	list_replace_and_destroy_element(listaTablaPags, posPuntero,
							tablaPag_create(new->pagina, new->marco, new->bitReferencia, new->bitModificado, 1),
							(void*) tablaPag_destroy);
}

int encontrarPosUso_cero_YModificado_uno(t_list *listaTablaPags)
{
	t_tablaPags *new;
	int i = 0;
	int encontrado = -1;
	int posPuntero, pag, marco, uso, mod;

	while( (i<list_size(listaTablaPags)) && encontrado==-1)
	{
		posPuntero = encontrarPosPuntero(listaTablaPags);

		new = list_get(listaTablaPags, posPuntero);

		pag = new->pagina;
		marco = new->marco;
		mod = new->bitModificado;
		uso = new->bitReferencia;

		if( (uso == 0) && (mod == 1) )
		{
			encontrado = posPuntero;
		}
		else
		{
			list_replace_and_destroy_element(listaTablaPags, posPuntero, tablaPag_create(pag, marco, 0, mod, 0),
					(void*) tablaPag_destroy);

			i++;
		}

		moverPuntero(listaTablaPags, posPuntero);
	}

	return encontrado;
}

int encontrarPosUsoYModificado_cero(t_list *listaTablaPags)
{
	t_tablaPags *new;
	int i = 0;
	int encontrado = -1;
	int posPuntero, pag, marco, uso, mod;

	while( (i<list_size(listaTablaPags)) && encontrado==-1)
	{
		posPuntero = encontrarPosPuntero(listaTablaPags);

		new = list_get(listaTablaPags, posPuntero);
		pag = new->pagina;
		marco = new->marco;
		mod = new->bitModificado;
		uso = new->bitReferencia;

		if( (uso == 0) && (mod == 0) )
		{
			encontrado = posPuntero;
		}
		else
		{list_replace_and_destroy_element(listaTablaPags, posPuntero, tablaPag_create(pag, marco, uso, mod, 0),
				(void*) tablaPag_destroy);
			i++;
		}

		moverPuntero(listaTablaPags, posPuntero);
	}

	return encontrado;
}

int encontrarPuntero(t_list *listaTablaPags)
{
	t_tablaPags *new;

	int i=0;
	int encontrado = -1;

	while( (i<list_size(listaTablaPags)) && encontrado!=0)
	{
		new = list_get(listaTablaPags,i);

		if(new->puntero == 1)
		{
			encontrado = 0;
		}
		else
		{
			list_replace_and_destroy_element(listaTablaPags, i, tablaPag_create(new->pagina, new->marco, 0, new->bitModificado, -1), (void*) tablaPag_destroy);

			i++;
		}
	}

	if(encontrado == 0)
	{
		return i;
	}

	return -1;
}

void ordenarPorMarco(t_list *listaTablaPags)
{
	bool _ayudantes_menor(t_tablaPags *joven, t_tablaPags *menos_joven) {
	return joven->marco < menos_joven->marco;
	}

	list_sort(listaTablaPags, (void*) _ayudantes_menor);
}

void rutinaFlushTLB()
{
	printf("Flush de TLB \n");

	pthread_mutex_lock(&mutexTLB);
	list_clean_and_destroy_elements(listaTLB, (void*) TLB_destroy);
	pthread_mutex_unlock(&mutexTLB);

	mostrarTLB();
}

void rutinaLimpiarMemoriaPrincipal()
{
	log_info(logger, "Limpiar la Memoria Principal");

	int i;
	t_tablaDeProcesos* new;

	pthread_mutex_lock(&mutexMemoFlush);

	rutinaFlushTLB();

	for(i=0; i<list_size(tablaDeProcesos); i++)
	{
		new = list_get(tablaDeProcesos, i);

		list_clean_and_destroy_elements(new->tablaDePaginas, (void*) tablaPag_destroy);
	}

	pthread_mutex_unlock(&mutexMemoFlush);
}

void dumpMemoriaPrincipal()
{
	printf("Dump de la memoria principal \n");

	pid_t childPID;
	int status;
	childPID = fork();

	if(childPID >= 0) // fork was successful
	{
		if(childPID == 0) // child process
		{
			mostrarMemoriaPpal();
		}
		else
		{
			waitpid(childPID, &status, WUNTRACED | WCONTINUED);
		}
	}
	else // fork failed
	{
		printf("\n Fork failed, quitting!!!!!!\n");
	}
}

void tlbHistorica()
{
	while(totalAccesos!=-1)
	{
		sleep(60);

		log_info(logger, "Total de accesos: %.0f. Total de hits: %.0f. Tasa de aciertos: %.2f", totalAccesos, totalTLBHits, totalTLBHits/totalAccesos);
	}

	log_info(logger, "Total de accesos: %.0f. Total de hits: %.0f. Tasa de aciertos: %.2f", totalAccesos, totalTLBHits, totalTLBHits/totalAccesos);
}

void mostrarTablaDePags(int pid) {
	int i;

	t_tablaDeProcesos *new;
	t_tablaPags* new2;

	new = buscarPID(pid);

	if(new!=NULL)
	{
		if(list_is_empty(new->tablaDePaginas))
		{
			log_info(logger, "Tabla vacia");
		}
		else
		{
			log_info(logger, "mProc: %d", pid);

			if(strncmp(politicaDeReemplazo, "CLOCK", 5)==0)
			{
				log_info(logger, "Pag  Marco  Posicion  Referencia  Modificado	Puntero");

				for (i = 0; i < list_size(new->tablaDePaginas); i++)
				{
					new2 = list_get(new->tablaDePaginas, i);

					log_info(logger, "%4d%12d%16d%22d%25d%27d", new2->pagina, new2->marco,	i + 1, new2->bitReferencia, new2->bitModificado, new2->puntero);
				}
			}
			else
			{
				log_info(logger, "Pag  Marco  Modificado");

				for (i = 0; i < list_size(new->tablaDePaginas); i++)
				{
					new2 = list_get(new->tablaDePaginas, i);

					log_info(logger, "%4d%12d%16d", new2->pagina, new2->marco, new2->bitModificado);
				}
			}
		}
	}
}

void mostrarTLB()
{
	int i;

	t_TLB* new;
	log_info(logger, "TLB");

	if(list_is_empty(listaTLB))
	{
		log_info(logger, "TLB Vacia");
	}
	else
	{
		log_info(logger, "Pid  Pagina  Marco");
		for (i = 0; i < list_size(listaTLB); i++)
		{
			new = list_get(listaTLB,i);

			log_info(logger, "%3d%10d%12d", new->pid, new->pagina, new->marco);
		}
	}
}

void mostrarMemoriaPpal()
{
	int i, j;

	t_tablaDeProcesos *new;
	t_tablaPags* new2;

	log_info(logger, "Marco  Content");

	for(j=0; j<list_size(tablaDeProcesos);j++)
	{
		new = list_get(tablaDeProcesos, j);

		for (i = 0; i < list_size(new->tablaDePaginas); i++)
		{
			if(!list_is_empty(new->tablaDePaginas))
			{
				new2 = list_get(new->tablaDePaginas, i);

				log_info(logger, "%4d%9s", new2->marco, memoriaPrincipal+(new2->marco*tamMarcos));
			}
		}
	}
}

static t_tablaPags *tablaPag_create(int pagina, int marco, int referencia, int modificado, char puntero)
{
	t_tablaPags *new = malloc(sizeof(t_tablaPags));
	new->pagina = pagina;
	new->marco = marco;
	new->bitReferencia = referencia;
	new->bitModificado = modificado;
	new->puntero = puntero;
	return new;
}

static void tablaPag_destroy(t_tablaPags *self) {
	free(self);
}

static t_fallosPid *fallos_create(int pid, int fallos, int accesos)
{
	t_fallosPid *new = malloc(sizeof(t_fallosPid));
	new->pid = pid;
	new->fallos = fallos;
	new->accedidas = accesos;

	return new;
}

static void fallos_destroy(t_fallosPid *self) {
	free(self);
}

static t_tablaDeProcesos *tablaProc_create(int pid, int pagina)
{
	t_tablaDeProcesos *new = malloc(sizeof(t_tablaDeProcesos));
	new->pid = pid;
	new->paginas = pagina;
	new->tablaDePaginas = list_create();

	return new;
}

static void tablaProc_destroy(t_tablaDeProcesos *self)
{
	list_destroy_and_destroy_elements(self->tablaDePaginas, (void*) tablaPag_destroy);
	free(self);
}

static t_TLB *TLB_create(int pid, int pagina, int marco)
{
	t_TLB *new = malloc(sizeof(t_TLB));
	new->pid = pid;
	new->pagina = pagina;
	new->marco = marco;

	return new;
}

static void TLB_destroy(t_TLB *self)
{
	free(self);
}

int tamanioOrdenCPU1(t_orden_CPU mensaje) {
	return (sizeof(mensaje.pid) + sizeof(mensaje.pagina) + sizeof(mensaje.orden)
			+ sizeof(mensaje.contentSize));
}

int tamanioOrdenCPU(t_orden_CPU mensaje) {
	return (sizeof(mensaje.pid) + sizeof(mensaje.pagina) + sizeof(mensaje.orden)
			+ sizeof(mensaje.contentSize) + mensaje.contentSize);
}
