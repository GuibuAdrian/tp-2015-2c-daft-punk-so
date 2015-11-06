/*
 * servidorM.c
 *
 *  Created on: 12/10/2015
 *      Author: utnso
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <signal.h>

#include <commons/collections/list.h>
#include <commons/collections/queue.h>
#include <commons/socket.h>
#include <commons/config.h>
#include <commons/log.h>

#define BACKLOG 5			// Define cuantas conexiones vamos a mantener pendientes al mismo tiempo
#define PACKAGESIZE 1024	// Define cual va a ser el size maximo del paquete a enviar

//a cada PID se le asocia una tabla de paginas luego se puede hacer lecturas o modificaciones
typedef struct {
	int pagina;
	int marco;
	char bitPresencia;
} t_tablaPags;

typedef struct {
	int pid;
	t_queue tablaDePaginas;
} t_tablaDeProcesos;

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
//t_queue *listaTablaPags; esto ya no ese global ya que depende de cada PID
t_list *tablaDeProcesos;
int socketSwap;
char *TLBHabil;
int maxMarcos, cantMarcos, tamMarcos, entradasTLB, retardoMem;
void* memoriaPrincipal;
t_list *espacioDeMemoria;
t_TLB *TLB;

static t_tablaDeProcesos *tablaProc_create(int pid, int pagina, int marco);
static void tablaProc_destroy(t_tablaDeProcesos *self);
static t_tablaPags *tablaPag_create(int pagina, int marco);
static void tablaPag_destroy(t_tablaPags *self);
int tamanioOrdenCPU(t_orden_CPU mensaje);
int tamanioOrdenCPU1(t_orden_CPU mensaje);

void recibirConexiones1(char * PUERTO_CPU);
t_orden_CPU enviarOrdenASwap(int pid, int orden, int paginas, char *content);
void enviarRespuestaCPU(t_orden_CPU respuestaMemoria, int socketCPU);

int main() {
	printf("\n");
	printf("~~~~~~~~~~MEMORIA~~~~~~~~~~\n\n");

	signal(SIGUSR1, rutinaDeSeniales);
	signal(SIGUSR2, rutinaDeSeniales);
	signal(SIGPOLL, rutinaDeSeniales);

	logger = log_create(
			"/home/utnso/github/tp-2015-2c-daft-punk-so/memoria/logsTP",
			"Memoria", true, LOG_LEVEL_INFO);

	t_config* config;

	config = config_create(
			"/home/utnso/github/tp-2015-2c-daft-punk-so/memoria/config.cfg");

	char *IP = config_get_string_value(config, "IP_SWAP");
	char * PUERTO_SWAP = config_get_string_value(config, "PUERTO_SWAP");
	maxMarcos = config_get_int_value(config, "MAXIMO_MARCOS_POR_PROCESO");
	cantMarcos = config_get_int_value(config, "CANTIDAD_MARCOS");
	tamMarcos = config_get_int_value(config, "TAMANIO_MARCO");
	entradasTLB = config_get_int_value(config, "ENTRADAS_TLB");
	retardoMem = config_get_int_value(config, "RETARDO_MEMORIA");
	TLBHabil = config_get_string_value(config, "TLB_HABILITADA");
	char *politicaDeReemplazo = config_get_string_value(config,
			"POLITICA_DE_REEMPLAZO");

	socketSwap = conectarse(IP, PUERTO_SWAP);

	char * PUERTO_CPU = config_get_string_value(config, "PUERTO_CPU");

	memoriaPrincipal = malloc(cantMarcos * tamMarcos);
	TLB = malloc(sizeof(TLB) * entradasTLB);

	//listaTablaPags = queue_create();
	tablaDeProcesos = list_create();

	if ((espacioDeMemoria = malloc(cantMarcos * tamMarcos)) == NULL) {
		log_error(logger,
				"There is no enough space in memory for the stucture \n");
	} else {
		espacioDeMemoria = list_create();
	}

	recibirConexiones1(PUERTO_CPU, politicaDeReemplazo);

	list_destroy_and_destroy_elements(tablaDeProcesos,
			(void*) tablaProc_destroy);
	close(socketSwap);
	free(memoriaPrincipal);
	free(TLB);

	config_destroy(config);
	log_info(logger, "---------------------FIN---------------------");
	log_destroy(logger);

	return 0;
}

static void tablaProc_destroy(t_tablaDeProcesos *self) {
	queue_clean_and_destroy_elements(tablaDeProcesos->head,
			(void*) tablaPag_destroy);
	free(self);
}

void recibirConexiones1(char * PUERTO_CPU, char *politica) {
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

		if (result == 0) {
			log_error(logger, "select() timed out!\n");
		} else if (result < 0 && errno != EINTR) {
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

					if (result > 0) {// aqui se reciben las ordenes?
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

						log_info(logger, "PID %d", mensaje.pid);
						log_info(logger, "Orden %d", mensaje.orden);
						log_info(logger, "Paginas %d", mensaje.pagina);

						if (strncmp(TLBHabil, "NO", 2) == 0) {
							mensaje = enviarOrdenASwap(mensaje.pid,
									mensaje.orden, mensaje.pagina,
									mensaje.content);
							if (mensaje.orden == 0){// aca se interprentan las ordenes de CPU
								//inicia un proceso
								list_add(tablaDeProcesos,tablaProc_create(mensaje.pid,mensaje.pagina,malloc(sizeof(int))));
							}else if (mensaje.orden == 1){
								// leer pagina de un proceso
								enviarRespuestaCPU(mensaje, socketCPU);
							}else if (mensaje.orden == 2){
								// escribe pagina de un proceso
								enviarRespuestaCPU(mensaje, socketCPU);
							}else{
								// finaliza el proceso
								// aca solo lo elimina de la tabla de procesos hay que pedirle al swap que lo elimine tambien.
								list_remove(tablaDeProcesos,getIndice(tablaDeProcesos,mensaje.pid));							}
								enviarRespuestaCPU(mensaje, socketCPU);
						} else {
							printf("TLB Habilitada :D\n");// aqui hay que almacenar las operaciones mas recientes
							if (mensaje.orden == 0){// aca se interprentan las ordenes de CPU
								//inicia un proceso
								list_add(tablaDeProcesos,tablaProc_create(mensaje.pid,mensaje.pagina,malloc(sizeof(int))));
							}else if (mensaje.orden == 1){
								// leer pagina de un proceso
								enviarRespuestaCPU(mensaje, socketCPU);
							}else if (mensaje.orden == 2){
								// escribe pagina de un proceso
								enviarRespuestaCPU(mensaje, socketCPU);
							}else{
								// finaliza el proceso
								// aca solo lo elimina de la tabla de procesos hay que pedirle al swap que lo elimine tambien.
								list_remove(tablaDeProcesos,getIndice(tablaDeProcesos,mensaje.pid));							}
								enviarRespuestaCPU(mensaje, socketCPU);
						}

						if (tablaDeProcesos == NULL) {
							//no se de donde o como obtener el marco =S
							tablaProc_create(mensaje.pid, mensaje.pagina,
									malloc(sizeof(int)));
						} else {
							buscarPID(mensaje.pid, mensaje.pagina, politica);
						}



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

int getIndice(t_list tablaDeProcesos,int pid){
	int index=0;
	t_list tablaCopia = tablaDeProcesos;
	t_tablaDeProcesos elemento = tablaCopia.head->data;

	while( elemento.pid != pid && index < tablaCopia.elements_count){
		index++;
		tablaCopia = tablaCopia.head->next;
	}

	return index;
}

void buscarPID(int pid, int pagina, char *politica) {
	t_list copiaTablaDeProcesos = tablaDeProcesos;

	t_tablaDeProcesos estructuraDeProceso = copiaTablaDeProcesos->head->data;
	int pidCopia = estructuraDeProceso.pid;
	while (pidCopia != pid) {
		estructuraDeProceso = copiaTablaDeProcesos.head->next->data;
		copiaTablaDeProcesos = copiaTablaDeProcesos.head->next;
		pidCopia = copiaTablaDeProcesos.head.data;
	}
	if (pidCopia != pid) {
		//sumamos un pid a la lista de procesos
		estructuraDeProceso.pid = pid;
		estructuraDeProceso.tablaDePaginas = tablaPag_create(pagina,
				malloc(sizeof(int)));
		list_add(tablaDeProcesos, estructuraDeProceso);
	} else {
		//aplicamos la politica correspondiente
		t_tablaPags paginaAActualizar;
		paginaAActualizar.pagina = pagina;
		paginaAActualizar.marco = malloc(sizeof(int));
		paginaAActualizar.bitPresencia = 1;
		if (politica == "FIFO") {
			reemplazoFIFO(paginaAActualizar,
					estructuraDeProceso.tablaDePaginas);
		} else if (politica == "LRU") {
			reemplazoLRU(paginaAActualizar, estructuraDeProceso.tablaDePaginas);
		} else if (politica == "CLOCK_MEJORADO") {
			//TODO diseñar el algoritmo de CLOCK MEJORADO
			reemplazoCLOCKMEJORADO(paginaAActualizar,
					estructuraDeProceso.tablaDePaginas);
		}

	}
}

void enviarRespuestaCPU(t_orden_CPU respuestaMemoria, int socketCPU) {
	t_orden_CPU mensajeSwap;

	mensajeSwap.pid = respuestaMemoria.pid;
	mensajeSwap.orden = respuestaMemoria.orden;
	mensajeSwap.pagina = respuestaMemoria.pagina;
	mensajeSwap.contentSize = strlen(respuestaMemoria.content) + 1;
	strcpy(mensajeSwap.content, respuestaMemoria.content);

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
					+ sizeof(mensajeSwap.contentSize), &mensajeSwap.content,
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
					+ sizeof(mensajeSwap.contentSize), &mensajeSwap.content,
			mensajeSwap.contentSize);

	send(socketSwap, mensajeSwapPackage, tamanioOrdenCPU(mensajeSwap), 0);

	free(mensajeSwapPackage);

	return recibirRespuestaSwap(socketSwap);
}

static t_tablaPags *tablaPag_create(int pagina, int marco) {
	t_tablaPags *new = malloc(sizeof(t_tablaPags));
	new->pagina = pagina;
	new->marco = marco;

	return new;
}

static t_tablaDeProcesos *tablaProc_create(int pid, int pagina, int marco) {
	t_tablaDeProcesos *new = malloc(sizeof(t_tablaDeProcesos));
	new->pid = pid;
	new->tablaDePaginas = tablaPag_create(pagina, marco);

	return new;
}

static void tablaPag_destroy(t_tablaPags *self) {
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

void rutinaDeSeniales(int senial) {
	pthread_t hiloFlushTLB;
	pthread_t hiloLimpiezaMemoriaPrincipal;
	void vacio;
	long pid;

	switch (senial) {
	case SIGUSR1:
		printf("Flush de TLB \n");
		pthread_create(&hiloFlushTLB, NULL, tablaPag_destroy, &TLB);//Calculo que Flush TLB es un destroy.
		break;
	case SIGUSR2:
		printf("Limpiar la Memoria Principal \n");
		pthread_create(&hiloLimpiezaMemoriaPrincipal, NULL,
				limpiarMemoriaPrincipal, vacio);
		break;
	case SIGPOLL:
		printf("Dump de la memoria principal \n");
		//Se sugiere un fork D:
		if (fork() == 0) {
			//Dumpeamos la memoria
			dumpMemory();
			exit(0);
		} else {
			wait(pid);
			exit(0);
		}
		break;
	}
}

void limpiarMemoriaPrincipal(void) {
	list_destroy(espacioDeMemoria);
	free(espacioDeMemoria);
}

void dumpMemory() {
	//Hay que recorrer la lista de espacioDeMemoria y levantando marcos y logearlos e ir liberando la memoria.
}

void reemplazoFIFO(t_tablaPags paginaAReemplazar, t_queue listaTablaPags) {
	t_queue copiaListaTablaPags = listaTablaPags;

	while (copiaListaTablaPags.elements->head->data != paginaAReemplazar) {
		copiaListaTablaPags = copiaListaTablaPags.elements->head->next;
	}

	if (copiaListaTablaPags.elements->head->data
			!= paginaAReemplazar&& copiaListaTablaPags.elements->head->next == NULL) {
		queue_pop(listaTablaPags);
		queue_push(listaTablaPags, paginaAReemplazar);
	}
}

void reemplazoLRU(t_tablaPags paginaAReemplazar, t_queue listaTablaPags) {
	t_queue copiaListaTablaPags = listaTablaPags;

	while (copiaListaTablaPags.elements->head->data != paginaAReemplazar) {
		copiaListaTablaPags = copiaListaTablaPags.elements->head->next;
	}
	//hay que borrarlo de esta posicion y ponerlo al final de la cola
	//sino eliminamos el primero de la cola y agregamos la pagina al final
	if (copiaListaTablaPags.elements->head->data == paginaAReemplazar) {
		copiaListaTablaPags.elements->head->next =
				copiaListaTablaPags.elements->head->next->next;
		queue_push(listaTablaPags, paginaAReemplazar);
	} else {
		queue_pop(listaTablaPags);
		queue_push(listaTablaPags, paginaAReemplazar);
	}
}
