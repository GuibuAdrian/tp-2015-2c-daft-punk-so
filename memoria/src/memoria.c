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
} t_tablaPags;

typedef struct {
	int pid;
	int paginas;
	t_queue *tablaDePaginas;
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
t_list *tablaDeProcesos;
int socketSwap;
char *TLBHabil;
int maxMarcos, cantMarcos, tamMarcos, entradasTLB, retardoMem;
void* memoriaPrincipal;
char *politicaDeReemplazo;
t_list *espacioDeMemoria;
t_TLB *TLB;
t_queue* colaAux;

static t_tablaDeProcesos *tablaProc_create(int pid, int pagina);
static void tablaProc_destroy(t_tablaDeProcesos *self);
static t_tablaPags *tablaPag_create(int pagina, int marco);
static void tablaPag_destroy(t_tablaPags *self);
int tamanioOrdenCPU(t_orden_CPU mensaje);
int tamanioOrdenCPU1(t_orden_CPU mensaje);

t_tablaPags* encontrarPagEnMemoriaPpal(int pid, int pagina);
t_tablaPags* buscarPagEnTablaDePags(int pagina, t_tablaDeProcesos* new);
t_tablaDeProcesos* buscarPID(int pid);

void recibirConexiones1(char * PUERTO_CPU);
t_orden_CPU enviarOrdenASwap(int pid, int orden, int paginas, char *content);
void enviarRespuestaCPU(t_orden_CPU respuestaMemoria, int socketCPU);
void procesarOrden(t_orden_CPU mensaje, int socketCPU);
void liberarTablaDePags();
void mostrarTablaDePags(int pid);

int main() {
	printf("\n");
	printf("~~~~~~~~~~MEMORIA~~~~~~~~~~\n\n");
/*
	signal(SIGUSR1, rutinaDeSeniales);
	signal(SIGUSR2, rutinaDeSeniales);
	signal(SIGPOLL, rutinaDeSeniales);
*/
	logger = log_create(
			"logsTP",
			"Memoria", true, LOG_LEVEL_INFO);

	t_config* config;

	config = config_create(
			"config.cfg");

	char *IP = config_get_string_value(config, "IP_SWAP");
	char * PUERTO_SWAP = config_get_string_value(config, "PUERTO_SWAP");
	maxMarcos = config_get_int_value(config, "MAXIMO_MARCOS_POR_PROCESO");
	cantMarcos = config_get_int_value(config, "CANTIDAD_MARCOS");
	tamMarcos = config_get_int_value(config, "TAMANIO_MARCO");
	entradasTLB = config_get_int_value(config, "ENTRADAS_TLB");
	retardoMem = config_get_int_value(config, "RETARDO_MEMORIA");
	TLBHabil = config_get_string_value(config, "TLB_HABILITADA");
	politicaDeReemplazo = config_get_string_value(config,
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

	recibirConexiones1(PUERTO_CPU);

	liberarTablaDePags();
	list_destroy_and_destroy_elements(tablaDeProcesos,(void*) tablaProc_destroy);
	close(socketSwap);
	free(memoriaPrincipal);
	free(TLB);

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

						if (strncmp(TLBHabil, "NO", 2) == 0)
						{
							procesarOrden(mensaje, socketCPU);
						}
						else
						{
							/*
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
							*/
						}
/*
						if (tablaDeProcesos == NULL) {
							//no se de donde o como obtener el marco =S
							tablaProc_create(mensaje.pid, mensaje.pagina,
									malloc(sizeof(int)));
						} else {
							buscarPID(mensaje.pid, mensaje.pagina, politica);
						}
*/


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

void fifo(t_queue *listaTablaPags, int pag, int marco)
{
	t_tablaPags* new2;

	new2 = queue_pop(listaTablaPags);
	queue_push(listaTablaPags, tablaPag_create(pag, new2->marco));
}

t_tablaPags* reemplazarPag(t_tablaDeProcesos* new, int pag)
{
	//if(strncmp(algoritmoReemplazo, "FIFO",4))
	t_tablaPags* new2;

	log_info(logger,"FIFO");

	new2 = queue_pop(new->tablaDePaginas);
	queue_push(new->tablaDePaginas, tablaPag_create(pag, new2->marco));

	return new2;
}

int buscarMarcoEnTablaDePags(t_tablaDeProcesos* new, int marcoBuscado)
{
	int i = 0, encontrado=-1;
	t_tablaPags* new2;

	while( (i<queue_size(new->tablaDePaginas)) )
	{
		new2 = queue_pop(new->tablaDePaginas);

		queue_push(	new->tablaDePaginas, new2);

		if( (encontrado==-1) && (new2->marco==marcoBuscado) )
		{
			encontrado = 1; //Encontre el marco buscado en memoria. Devuelvo 1
		}

		i++;
	}

	return encontrado;
}

int	asignarMarco()
{
	int i=0, encontrado = -1, marcoBuscado = 0;
	t_tablaDeProcesos* new;

	while( (i<list_size(tablaDeProcesos)) && (encontrado == -1) && (marcoBuscado<cantMarcos) )
	{
		while( (i<list_size(tablaDeProcesos)) && (encontrado == -1) )
		{
			new = list_get(tablaDeProcesos, i);

			encontrado = buscarMarcoEnTablaDePags(new, marcoBuscado);

			i++;
		}

		if(encontrado==1)
		{
			i=0;
			marcoBuscado++;
			encontrado=-1;
		}
		else
		{
			break;
		}
	}

	if( (marcoBuscado>=cantMarcos) )
	{
		return -1;
	}
	else
	{
		if( (i>=list_size(tablaDeProcesos)) && (encontrado==-1) )
		{
			return marcoBuscado;
		}
		else
		{
			return -1;
		}

	}
}

int actualizarMemoriaPpal(t_tablaDeProcesos* new, int pag)
{
	t_tablaPags* new2;
	int totalPag = queue_size(new->tablaDePaginas);
	int marco;

	if(totalPag==maxMarcos) //Si la cantidad de marcos ocupados es MAX entonces empiezo a reemplazar
	{
		log_info(logger,"Reemplazando");

		new2 = reemplazarPag(new, pag);

		marco = new2->marco;
	}
	else
	{
		marco = asignarMarco(); //-1 No puedo asignarle marcos

		if(marco==-1)
		{
			return marco;
		}
		queue_push(new->tablaDePaginas, tablaPag_create(pag, marco));
	}

	mostrarTablaDePags(new->pid);

	return marco;
}

void iniciarProceso(int pid, int paginas)
{
	list_add(tablaDeProcesos,tablaProc_create(pid,paginas));

	log_info(logger,"Proceso %d iniciado de %d pags", pid, paginas);
}

void finalizarProceso(int pid)
{
	int i;
	t_tablaDeProcesos* new;

	for(i = 0; i < list_size(tablaDeProcesos); i++) //Borro al pid de la tabla de pags
	{
		new = list_get(tablaDeProcesos, i);

		if(new->pid==pid)
		{
			queue_destroy_and_destroy_elements(new->tablaDePaginas, (void*)tablaPag_destroy);

			list_remove_and_destroy_element(tablaDeProcesos, i, (void*) tablaProc_destroy);

			i--;
		}
	}

	for(i=0; i<entradasTLB; i++) //Borro al pid de la TLB
	{
		if (TLB[i].pid==pid)
		{
			TLB[i].pid = -1;
		}
	}

	log_info(logger,"Proceso %d finalizado", pid);
}

void procesarOrden(t_orden_CPU mensaje, int socketCPU)
{
	t_tablaDeProcesos* new;
	t_tablaPags* new2;
	t_orden_CPU respuestaSwap;

	if (mensaje.orden == 0)
	{
		iniciarProceso(mensaje.pid, mensaje.pagina);

		respuestaSwap = enviarOrdenASwap(mensaje.pid, mensaje.orden, mensaje.pagina, mensaje.content);

		enviarRespuestaCPU(respuestaSwap, socketCPU);
	}
	else
		if(mensaje.orden==3)
		{
			finalizarProceso(mensaje.pid);	//Borro al pid de la TLB y la tabla de pags

			respuestaSwap = enviarOrdenASwap(mensaje.pid, mensaje.orden, mensaje.pagina, mensaje.content);

			enviarRespuestaCPU(respuestaSwap, socketCPU);
		}
	else
	{
		new2 = encontrarPagEnMemoriaPpal(mensaje.pid, mensaje.pagina);

		if(new2 != NULL) //Si esta en Memoria Principal
		{
			log_info(logger, "Esta en memoria principal");

			if (mensaje.orden == 1)	// leer pagina de un proceso
			{
				respuestaSwap.pid = mensaje.pid;
				respuestaSwap.pagina = mensaje.pagina;
				respuestaSwap.orden = 2;
				strncpy(respuestaSwap.content, memoriaPrincipal+new2->marco*tamMarcos, tamMarcos);
				respuestaSwap.contentSize = strlen(respuestaSwap.content)+1;

				log_info(logger,"Proceso %d leyendo pag: %d. Contenido: %s", respuestaSwap.pid, respuestaSwap.pagina, respuestaSwap.content);

				enviarRespuestaCPU(respuestaSwap, socketCPU);//Le devuelvo el contenido del marco al CPU

				//Actualizo TLB
				enviarRespuestaCPU(mensaje, socketCPU);
			}
			else
				if (mensaje.orden == 2)// escribe pagina de un proceso
				{
					strncpy(memoriaPrincipal+new2->marco*tamMarcos, respuestaSwap.content, tamMarcos);//Actualizo la memo ppal

					log_info(logger,"Proceso %d Escribiendo: %s en pag: %d", mensaje.pid, memoriaPrincipal+new2->marco*tamMarcos, mensaje.pagina);

					respuestaSwap = enviarOrdenASwap(mensaje.pid, mensaje.orden, new2->pagina, mensaje.content); //Le aviso al SWAP del nuevo contenido//Le aviso al SWAP del nuevo contenido
					enviarRespuestaCPU(respuestaSwap, socketCPU);//Le devuelvo el contenido del marco al CPU

					//Actualizo TLB
					enviarRespuestaCPU(mensaje, socketCPU);
				}
		}
		else  //No esta en Memoria Principal. La traigo desde SWAP
		{
			log_info(logger, "No esta en memoria principal");

			new = buscarPID(mensaje.pid);

			int marco = actualizarMemoriaPpal(new, mensaje.pagina);

			if(marco == -1)
			{
				mensaje.orden=1;
				enviarRespuestaCPU(mensaje, socketCPU);
			}
			else
			{
				log_info(logger,"mProc: %d, Pag: %d, Marco asignado: %d",mensaje.pid, mensaje.pagina, marco);

				if (mensaje.orden == 1)	// leer pagina de un proceso
				{
					log_info(logger,"Solicitando mProc: %d Pag: %d a SWAP", mensaje.pid, mensaje.pagina);

					respuestaSwap = enviarOrdenASwap(mensaje.pid, mensaje.orden, mensaje.pagina, mensaje.content);

					strncpy(memoriaPrincipal+marco*tamMarcos, respuestaSwap.content, tamMarcos);
					respuestaSwap.contentSize = strlen(respuestaSwap.content)+1;

					log_info(logger,"Proceso %d leyendo pag: %d, contenido: %s", mensaje.pid, mensaje.pagina, respuestaSwap.content);

					enviarRespuestaCPU(respuestaSwap, socketCPU);
				}
				else
				{
					if (mensaje.orden == 2)  // escribe pagina de un proceso
					{
						log_info(logger,"Solicitando mProc: %d Pag: %d a SWAP", mensaje.pid, mensaje.pagina);

						respuestaSwap = enviarOrdenASwap(mensaje.pid, mensaje.orden, mensaje.pagina, mensaje.content); //Le aviso al SWAP del nuevo contenido
						strncpy(memoriaPrincipal+marco*tamMarcos, mensaje.content, tamMarcos);//Actualizo la memo ppal

						log_info(logger,"Proceso %d Escribiendo: %s en pag: %d", mensaje.pid, mensaje.content, mensaje.pagina);

						enviarRespuestaCPU(respuestaSwap, socketCPU);
					}
				}//else escribir
			}//else fallo asignando marco
		}//else no esta en memoria
	}//else orden de incio/fin
}


/*
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
*/

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

t_tablaPags* encontrarPagEnMemoriaPpal(int pid, int pagina)
{
	t_tablaDeProcesos* new = buscarPID(pid);
	t_tablaPags* new2;

	new2 = buscarPagEnTablaDePags(pagina, new);

	return new2;
}

t_tablaPags* buscarPagEnTablaDePags(int pagina, t_tablaDeProcesos* new)
{
	int i=0;

	t_tablaPags* new2;

	while(i<queue_size(new->tablaDePaginas))
	{
		new2 = queue_pop(new->tablaDePaginas);

		if(new2->pagina == pagina)
		{
			queue_push(new->tablaDePaginas,new2);

			break;
		}

		queue_push(new->tablaDePaginas,new2);

		i++;
	}

	if( (queue_size(new->tablaDePaginas)==0) || (new2->pagina != pagina))
	{
		return NULL;
	}
	else
	{
		return new2;
	}
}

t_tablaDeProcesos* buscarPID(int pid)
{
	bool compararPorIdentificador2(t_tablaDeProcesos *unaCaja)
	{
		if (unaCaja->pid == pid)
		{
			return 1;
		}

		return 0;
	}
	return list_find(tablaDeProcesos, (void*) compararPorIdentificador2);
}

/*
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
*/

/*
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
*/
static t_tablaPags *tablaPag_create(int pagina, int marco) {
	t_tablaPags *new = malloc(sizeof(t_tablaPags));
	new->pagina = pagina;
	new->marco = marco;

	return new;
}
static void tablaPag_destroy(t_tablaPags *self)
{
	free(self);
}

static t_tablaDeProcesos *tablaProc_create(int pid, int pagina) {
	t_tablaDeProcesos *new = malloc(sizeof(t_tablaDeProcesos));
	new->pid = pid;
	new->paginas = pagina;
	new->tablaDePaginas = queue_create();

	return new;
}
static void tablaProc_destroy(t_tablaDeProcesos *self)
{
	free(self);
}

void liberarTablaDePags()
{
	int i;
	t_tablaDeProcesos *new;

	for(i=0; i<list_size(tablaDeProcesos); i++)
	{
		new = list_get(tablaDeProcesos, i);

		queue_destroy_and_destroy_elements(new->tablaDePaginas, (void*) tablaPag_destroy);

	}
}

int tamanioOrdenCPU1(t_orden_CPU mensaje) {
	return (sizeof(mensaje.pid) + sizeof(mensaje.pagina) + sizeof(mensaje.orden)
			+ sizeof(mensaje.contentSize));
}

int tamanioOrdenCPU(t_orden_CPU mensaje) {
	return (sizeof(mensaje.pid) + sizeof(mensaje.pagina) + sizeof(mensaje.orden)
			+ sizeof(mensaje.contentSize) + mensaje.contentSize);
}

void mostrarTablaDePags(int pid)
{
	int i;

	t_tablaDeProcesos *new;
	t_tablaPags* new2;

	new = buscarPID(pid);



	log_info(logger, "mProc: %d", pid);
	log_info(logger, "Pag  Marco  Posicion");

	for(i=0;i<queue_size(new->tablaDePaginas);i++)
	{
		new2 = queue_pop(new->tablaDePaginas);
		queue_push(new->tablaDePaginas, tablaPag_create(new2->pagina, new2->marco));
		log_info(logger, " %d  		 %d     	   %d", new2->pagina, new2->marco, i+1);
	}
}

