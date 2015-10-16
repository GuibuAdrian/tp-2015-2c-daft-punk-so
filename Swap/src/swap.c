#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>

#include <commons/collections/list.h>
#include <commons/socket.h>
#include <commons/config.h>
#include <commons/log.h>

#define PACKAGESIZE 1024	// Define cual va a ser el size maximo del paquete a enviar

typedef struct
{
	int pid;
	int orden;	// 0=Iniciar, 1=Leer, 2=Escribir, 3=Finalizar
	int paginas;
	int contentSize;
	char content[PACKAGESIZE];
}t_orden_memoria;

typedef struct
{
	char* inicioHueco;
	int cantPag;
}t_espacioLibre;

typedef struct
{
	int pid;
	char* inicioSwap;
	int cantPag;
}t_espacioOcupado;

t_log* logger;
t_list *listaLibres, *listaOcupados;
int tamanio, cantPagSwap, tamanioPagSwap;
int socket_memoria;

static t_espacioLibre *libre_create(char* inicioHueco, int cantPag);
static void libre_destroy(t_espacioLibre *self);
static t_espacioOcupado *ocupado_create(int pid, char* inicioSwap, int cantPag);
static void ocupado_destroy(t_espacioOcupado *self);
int tamanio_archivo(int fd);
int tamanioRespuestaMemoria(t_orden_memoria unaPersona);

char* mapearArchivo();
void crearArchivoSwap(char * nombreSwap, int tamanioSwap, int cantSwap);
void procesarOrden(t_orden_memoria ordenMemoria);
void mostrarListas();
t_espacioOcupado* buscarPIDEnOcupados(int pid);

int main()
{
	printf("\n");
	printf("~~~~~~~~~~SWAP~~~~~~~~~~\n\n");


	logger = log_create("/home/utnso/github/tp-2015-2c-daft-punk-so/Swap/logsTP", "Swap", true, LOG_LEVEL_INFO);

	t_config* config;

	config = config_create("/home/utnso/github/tp-2015-2c-daft-punk-so/Swap/config.cfg");
	char *nombreSwap = config_get_string_value( config, "NOMBRE_SWAP");
	cantPagSwap = config_get_int_value( config, "CANTIDAD_PAGINAS");
	tamanioPagSwap = config_get_int_value( config, "TAMANIO_PAGINA");
	char * PUERTO_ESCUCHA = config_get_string_value(config, "PUERTO_ESCUCHA");


	listaLibres = list_create();
	listaOcupados = list_create();


	crearArchivoSwap(nombreSwap, tamanioPagSwap, cantPagSwap);
	printf("\n");

	char* mapeo = mapearArchivo();


	list_add(listaLibres, libre_create(mapeo, cantPagSwap));


	int listenningSocket = recibirLlamada(PUERTO_ESCUCHA);
	socket_memoria = aceptarLlamada(listenningSocket);


	log_info(logger, "Memoria conectado, ehhhh!!");


	int result = 1;

	while(result)
	{
		t_orden_memoria ordenMemoria;
		void* package = malloc(sizeof(int)*4);

		result = recv(socket_memoria, (void*)package, sizeof(ordenMemoria.pid), 0);
		if(result < 0)
		{
			break;
		}
		memcpy(&ordenMemoria.pid,package,sizeof(ordenMemoria.pid));
		recv(socket_memoria,(void*) (package+sizeof(ordenMemoria.pid)), sizeof(ordenMemoria.paginas), 0);
		memcpy(&ordenMemoria.orden, package+sizeof(ordenMemoria.pid),sizeof(ordenMemoria.orden));
		recv(socket_memoria,(void*) (package+sizeof(ordenMemoria.pid)+sizeof(ordenMemoria.orden)), sizeof(ordenMemoria.paginas), 0);
		memcpy(&ordenMemoria.paginas, package+sizeof(ordenMemoria.pid)+sizeof(ordenMemoria.orden),sizeof(ordenMemoria.paginas));
		recv(socket_memoria,(void*) (package+sizeof(ordenMemoria.pid)+sizeof(ordenMemoria.orden)+sizeof(ordenMemoria.paginas)), sizeof(ordenMemoria.contentSize), 0);
		memcpy(&ordenMemoria.contentSize, package+sizeof(ordenMemoria.pid)+sizeof(ordenMemoria.orden)+sizeof(ordenMemoria.paginas), sizeof(ordenMemoria.contentSize));


		void* package2=malloc(ordenMemoria.contentSize);

		recv(socket_memoria,(void*) package2, ordenMemoria.contentSize, 0);//campo longitud(NO SIZEOF DE LONGITUD)
		memcpy(&ordenMemoria.content, package2, ordenMemoria.contentSize);


		procesarOrden(ordenMemoria);

		free(package);
		free(package2);
	}

	log_info(logger, "---------------------FIN---------------------");

	list_destroy_and_destroy_elements(listaOcupados,(void*) ocupado_destroy);
	list_destroy_and_destroy_elements(listaLibres,(void*) libre_destroy);

	log_destroy(logger);
	config_destroy(config);

	close(listenningSocket);
	close(socket_memoria);
    munmap( mapeo, tamanio );

    return 0;
}

void respuestaMemoria(int pid, int paginas, int mensaje, char pagContent[PACKAGESIZE])
{
	t_orden_memoria respuestaMemoria;

	respuestaMemoria.pid = pid;
	respuestaMemoria.paginas = paginas;
	respuestaMemoria.orden = mensaje;
	respuestaMemoria.contentSize = strlen(pagContent);
	strcpy(respuestaMemoria.content ,pagContent);

	void* respuestaPackage = malloc(tamanioRespuestaMemoria(respuestaMemoria));

	memcpy(respuestaPackage,&respuestaMemoria.pid,sizeof(respuestaMemoria.pid));
	memcpy(respuestaPackage+sizeof(respuestaMemoria.pid), &respuestaMemoria.orden, sizeof(respuestaMemoria.orden));
	memcpy(respuestaPackage+sizeof(respuestaMemoria.pid)+sizeof(respuestaMemoria.orden),&respuestaMemoria.paginas,sizeof(respuestaMemoria.paginas));
	memcpy(respuestaPackage+sizeof(respuestaMemoria.pid)+sizeof(respuestaMemoria.orden)+sizeof(respuestaMemoria.paginas),&respuestaMemoria.contentSize,sizeof(respuestaMemoria.contentSize));
	memcpy(respuestaPackage+sizeof(respuestaMemoria.pid)+sizeof(respuestaMemoria.orden)+sizeof(respuestaMemoria.paginas)+sizeof(respuestaMemoria.contentSize), respuestaMemoria.content, respuestaMemoria.contentSize);

	send(socket_memoria, respuestaPackage, tamanioRespuestaMemoria(respuestaMemoria),0);

	free(respuestaPackage);
}

int reservarEspacio(int pid, int paginas)	// 0=Exito  1=Fracaso
{
	t_espacioLibre* libreAux = buscarEspacioAOcupar(paginas);

	if(libreAux!=NULL)
	{
		int posLibre = encontrarPosicionEspacioLibre(libreAux->inicioHueco);

		t_espacioLibre* espLibreViejo = list_replace(listaLibres, posLibre, libre_create(libreAux->inicioHueco+(paginas*tamanioPagSwap), libreAux->cantPag-paginas));

		list_add(listaOcupados, ocupado_create(pid, libreAux->inicioHueco, paginas));

		libre_destroy(espLibreViejo);

		return 0;
	}
	else
	{
		return 1;
	}
}

void procesarOrden(t_orden_memoria ordenMemoria)
{
	int respuesta;

	if (ordenMemoria.orden == 0)  // 0=Iniciar
	{
		log_info(logger, "Iniciando mProc: %d de %d paginas", ordenMemoria.pid, ordenMemoria.paginas);

		respuesta = reservarEspacio(ordenMemoria.pid, ordenMemoria.paginas);

		if (respuesta)  // 0 = Exito, 1 = Fallo
		{
			respuestaMemoria(ordenMemoria.pid, ordenMemoria.paginas, 1, "/");
		}
		else
		{
			respuestaMemoria(ordenMemoria.pid, ordenMemoria.paginas, 0, "/");
		}
	}
	else
	{
		if (ordenMemoria.orden == 1) // 1=Leer
		{

			t_espacioOcupado* pidOcup = buscarPIDEnOcupados(ordenMemoria.pid);
			char * pagContent = malloc(4);

			strncpy(pagContent,pidOcup->inicioSwap+(ordenMemoria.paginas*4),4);

			respuestaMemoria(ordenMemoria.pid, ordenMemoria.paginas, 2, pagContent);

			log_info(logger, "Leyendo mProc: %d. Pagina %d: %s ", ordenMemoria.pid, ordenMemoria.paginas, pagContent);

			strncpy(pagContent,"",4);

			free(pagContent);
		}
		else
		{
			if (ordenMemoria.orden == 3) // 3=Finalizar
			{
				log_info(logger, "Finalizando mProc: %d", ordenMemoria.pid);

				respuestaMemoria(ordenMemoria.pid, ordenMemoria.paginas, 3, "/");
			}
			else
			{
				if (ordenMemoria.orden == 2) // 2=Escribir
				{
					int contentSize = strlen(ordenMemoria.content);

					t_espacioOcupado* pidOcup = buscarPIDEnOcupados(ordenMemoria.pid);

					strncpy(pidOcup->inicioSwap+(ordenMemoria.paginas*4), ordenMemoria.content, contentSize);

					log_info(logger, "Escribiendo mProc: %d. Pagina %d: %s ", ordenMemoria.pid, ordenMemoria.paginas, ordenMemoria.content);

					respuestaMemoria(ordenMemoria.pid, ordenMemoria.paginas, 4, ordenMemoria.content);
				}
			}
	} //else finalizar
	} //else leer
}

char* mapearArchivo()
{
	int mapper;
	char* mapeo;

	char* nombre_archivo = "/home/utnso/github/tp-2015-2c-daft-punk-so/Swap/Debug/swap.data";
	//char* nombre_archivo = "/home/utnso/arch.txt";

	if(( mapper = open (nombre_archivo, O_RDWR) ) == -1)
	{
		//Si no se pudo abrir, imprimir el error y abortar;
		log_error(logger, "Error al abrir el archivo '%s': %s\n", nombre_archivo, strerror(errno));
		abort();
	}
	tamanio = tamanio_archivo(mapper);
	if( (mapeo = mmap( NULL, tamanio, PROT_READ | PROT_WRITE, MAP_SHARED, mapper, 0 )) == MAP_FAILED)
	{
		//Si no se pudo ejecutar el MMAP, imprimir el error y abortar;
		log_error(logger, "Error al ejecutar MMAP del archivo '%s' de tamaño: %d: %s\n", nombre_archivo, tamanio, strerror(errno));
		abort();
	}

    close(mapper);

	return mapeo;
}

void crearArchivoSwap(char * nombreSwap, int tamanioSwap, int cantSwap)
{
	char strB[20];
	char strC[20];

	char strA[40] = " of=";
	char * strD = nombreSwap;
	sprintf(strB, " bs=%d", tamanioSwap);
	sprintf(strC, " count=%d", cantSwap);
	strcat(strB,strC);

	strcat(strA,strD);

	strcat(strA,strB);

	char strE[70] = "dd if=/dev/zero";

	strcat(strE,strA);

	printf("\n");

	system(strE);
}

static t_espacioLibre *libre_create(char* inicioHueco, int cantPag)
{
	t_espacioLibre *new = malloc(sizeof(t_espacioLibre));
	new->inicioHueco = inicioHueco;
	new->cantPag = cantPag;

	return new;
}
static void libre_destroy(t_espacioLibre *self)
{
    free(self);
}
static t_espacioOcupado *ocupado_create(int pid, char* inicioSwap, int cantPag)
{
	t_espacioOcupado *new = malloc(sizeof(t_espacioOcupado));
	new->pid = pid;
	new->inicioSwap = inicioSwap;
	new->cantPag = cantPag;

	return new;
}
static void ocupado_destroy(t_espacioOcupado *self)
{
    free(self);
}
int tamanio_archivo(int fd){
	struct stat buf;
	fstat(fd, &buf);
	return buf.st_size;
}
int tamanioRespuestaMemoria(t_orden_memoria unaPersona)
{
	return (sizeof(unaPersona.pid)+sizeof(unaPersona.paginas)+sizeof(unaPersona.orden)+sizeof(unaPersona.contentSize)+strlen(unaPersona.content));
};

void mostrarListas()
{
	t_espacioLibre* newL;
	int i;
	printf("Libres\n\n");
	for(i=0; i<list_size(listaLibres);i++)
	{
		newL = list_get(listaLibres,i);

		printf("%s\n", newL->inicioHueco);
		printf("CantPags: %d\n", newL->cantPag);

	}

	printf("\n");
	printf("Ocupados\n\n");

	t_espacioOcupado* newO;

	char string[30];
	for(i=0; i<list_size(listaOcupados);i++)
	{
		newO = list_get(listaOcupados,i);

		strncpy(string,"",30);

		strncpy(string,newO->inicioSwap,4*newO->cantPag);

		printf("PID: %d\n", newO->pid);
		printf("%s\n", string);
		printf("CantPags: %d\n", newO->cantPag);

	}

}

t_espacioOcupado* buscarPIDEnOcupados(int pid)
{
	bool compararPorIdentificador2(t_espacioOcupado *unaCaja)
	{
		if (unaCaja->pid == pid)
		{
			return 1;
		}

		return 0;
	}

	return list_find(listaOcupados, (void*) compararPorIdentificador2);
}
int encontrarPosicionEspacioLibre(char* inicioHueco)
{
	t_espacioLibre* new;

	int i=0;
	int encontrado = 1;

	while( (i<list_size(listaLibres)) && encontrado!=0)
	{
		new = list_get(listaLibres,i);

		if(new->inicioHueco == inicioHueco)
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
t_espacioLibre* buscarEspacioAOcupar(int cantPags)
{
	bool compararPorIdentificador2(t_espacioLibre *unaCaja)
	{
		if (unaCaja->cantPag >= cantPags)
		{
			return 1;
		}

		return 0;
	}

	return list_find(listaLibres, (void*) compararPorIdentificador2);
}
