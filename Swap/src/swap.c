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

typedef struct
{
	int pid;
	int paginas;
	int mensajeSize;
} t_respuesta_memoria;

t_log* logger;
t_list *listaLibres;
t_list *listaOcupados;
int tamanio, cantPagSwap, tamanioPagSwap;
int socket_memoria;

static t_espacioLibre *libre_create(char* inicioHueco, int cantPag);
static void libre_destroy(t_espacioLibre *self);
static t_espacioOcupado *ocupado_create(int pid, char* inicioSwap, int cantPag);
static void ocupado_destroy(t_espacioOcupado *self);
int tamanio_archivo(int fd);
int tamanioRespuestaMemoria(t_respuesta_memoria unaPersona);

char* mapearArchivo();
void crearArchivoSwap(char * nombreSwap, int tamanioSwap, int cantSwap);
void procesar(t_orden_memoria ordenMemoria);
void mostrarListas();
t_espacioOcupado* buscarPID(int pid);

int main()
{
	printf("\n");
	printf("----SWAP----\n\n");


	logger = log_create("logsTP", "SWAP", true, LOG_LEVEL_INFO);


	t_config* config;

	config = config_create("config.cfg");
	char *nombreSwap = config_get_string_value( config, "NOMBRE_SWAP");
	cantPagSwap = config_get_int_value( config, "CANTIDAD_PAGINAS");
	tamanioPagSwap = config_get_int_value( config, "TAMANIO_PAGINA");
	char * PUERTO_ESCUCHA = config_get_string_value(config, "PUERTO_ESCUCHA");


	listaLibres = list_create();
	listaOcupados = list_create();


	crearArchivoSwap(nombreSwap, tamanioPagSwap, cantPagSwap);
	printf("\n");
	log_info(logger, "Archivo %s creado ", nombreSwap);

	char* mapeo = mapearArchivo();


	list_add(listaLibres, libre_create(mapeo, cantPagSwap));


	int listenningSocket = recibirLlamada(PUERTO_ESCUCHA);
	socket_memoria = aceptarLlamada(listenningSocket);

	log_info(logger, "Conectado a Memoria, urra!");
	//printf("Memoria conectado\n\n");

	t_orden_memoria ordenMemoria;
	void* package = malloc(sizeof(int)*3);

	int result = 1;

	while(result)
	{
		//printf("Orden recibida\n");
		result = recv(socket_memoria, (void*)package, sizeof(ordenMemoria.pid), 0);
		memcpy(&ordenMemoria.pid,package,sizeof(ordenMemoria.pid));

		recv(socket_memoria,(void*) (package+sizeof(ordenMemoria.pid)), sizeof(ordenMemoria.paginas), 0);
		memcpy(&ordenMemoria.orden, package+sizeof(ordenMemoria.pid),sizeof(ordenMemoria.orden));

		recv(socket_memoria,(void*) (package+sizeof(ordenMemoria.pid)+sizeof(ordenMemoria.orden)), sizeof(ordenMemoria.paginas), 0);
		memcpy(&ordenMemoria.paginas, package+sizeof(ordenMemoria.pid)+sizeof(ordenMemoria.orden),sizeof(ordenMemoria.paginas));


		procesar(ordenMemoria);
	}


	//mostrarListas();

	log_info(logger, "-------------------------------------------------");


	free(package);

	list_destroy_and_destroy_elements(listaOcupados,(void*) ocupado_destroy);
	list_destroy_and_destroy_elements(listaLibres,(void*) libre_destroy);

	config_destroy(config);
	log_destroy(logger);


	close(listenningSocket);
	close(socket_memoria);
    munmap( mapeo, tamanio );

    return 0;
}

void respuestaMemoria(int pid, int paginas, int mensaje)
{
	t_respuesta_memoria respuestaMemoria;

	respuestaMemoria.pid = pid;
	respuestaMemoria.paginas = paginas;
	respuestaMemoria.mensajeSize = mensaje;

	void* respuestaPackage = malloc(tamanioRespuestaMemoria(respuestaMemoria));

	memcpy(respuestaPackage,&respuestaMemoria.pid,sizeof(respuestaMemoria.pid));
	memcpy(respuestaPackage+sizeof(respuestaMemoria.pid),&respuestaMemoria.paginas,sizeof(respuestaMemoria.paginas));
	memcpy(respuestaPackage+sizeof(respuestaMemoria.pid)+sizeof(respuestaMemoria.paginas), &respuestaMemoria.mensajeSize, sizeof(respuestaMemoria.mensajeSize));

	send(socket_memoria, respuestaPackage, tamanioRespuestaMemoria(respuestaMemoria),0);

	//printf( "Respuesta enviada\n");

	free(respuestaPackage);

}


int encontrarPosicionEnPCB(char* inicioHueco)
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
int reservarEspacio(int pid, int paginas)	// 1=Exito  0=Fracaso
{
	t_espacioLibre* libreAux = buscarEspacioAOcupar(paginas);

	if(libreAux!=NULL)
	{
		int posLibre = encontrarPosicionEnPCB(libreAux->inicioHueco);

		list_replace(listaLibres, posLibre, libre_create(libreAux->inicioHueco+(paginas*tamanioPagSwap), libreAux->cantPag-paginas));

		list_add(listaOcupados, ocupado_create(pid, libreAux->inicioHueco, paginas));

		return 1;
	}
	else
	{
		return 0;
	}
}


void procesar(t_orden_memoria ordenMemoria)
{
	int respuesta;

	if (ordenMemoria.orden == 0)
	{
		respuesta = reservarEspacio(ordenMemoria.pid, ordenMemoria.paginas);

		if (respuesta)
		{
			log_info(logger, "mProc asignado: %d. De %d paginas", ordenMemoria.pid, ordenMemoria.paginas);

		//	printf("mProc asignado: %d. De %d paginas\n", ordenMemoria.pid, ordenMemoria.paginas);
			respuestaMemoria(ordenMemoria.pid, ordenMemoria.paginas, 1);
		}
		else
		{
			respuestaMemoria(ordenMemoria.pid, ordenMemoria.paginas, 0);

		}


	}
	else
	{
		if (ordenMemoria.orden == 1)
		{
			t_espacioOcupado* pidOcup = buscarPID(ordenMemoria.pid);
			char * pagContent = malloc(4);

			strncpy(pagContent,pidOcup->inicioSwap+(ordenMemoria.paginas*4),4);
			//printf("Contenido: %s\n",pagContent);
			log_info(logger, "Lectura mProc: %d. Contenido: %s.", ordenMemoria.pid, pagContent);

			respuestaMemoria(ordenMemoria.pid, ordenMemoria.paginas, 2);
			strncpy(pagContent,"",4);

			free(pagContent);
		}
	else
	{
		if (ordenMemoria.orden == 3)
		{
		//	printf("mProc liberado: %d\n", ordenMemoria.pid);
			log_info(logger, "mProc liberado: %d", ordenMemoria.pid);

			respuestaMemoria(ordenMemoria.pid, ordenMemoria.paginas, 3);
		}
	} //else finalizar
	} //else leer


}


char* mapearArchivo()
{
	int mapper;
	char* mapeo;

	char* nombre_archivo = "/home/utnso/Escritorio/tp-2015-2c-daft-punk-so/Swap/Debug/swap.data";
	//char* nombre_archivo = "/home/utnso/arch.txt";

	if(( mapper = open (nombre_archivo, O_RDWR) ) == -1)
	{
		//Si no se pudo abrir, imprimir el error y abortar;
		fprintf(stderr, "Error al abrir el archivo '%s': %s\n", nombre_archivo, strerror(errno));
		abort();
	}
	tamanio = tamanio_archivo(mapper);
	if( (mapeo = mmap( NULL, tamanio, PROT_READ | PROT_WRITE, MAP_SHARED, mapper, 0 )) == MAP_FAILED)
	{
		//Si no se pudo ejecutar el MMAP, imprimir el error y abortar;
		fprintf(stderr, "Error al ejecutar MMAP del archivo '%s' de tamaño: %d: %s\n", nombre_archivo, tamanio, strerror(errno));
		abort();
	}


    close(mapper);

	return mapeo;
}


void crearArchivoSwap(char * nombreSwap, int tamanioSwap, int cantSwap)
{
	char strB[8];
	char strC[10];

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


	//printf("%s\n",strE);

	system(strE);

}


static t_espacioLibre *libre_create(char* inicioHueco, int cantPag)
{
	t_espacioLibre *new = malloc(sizeof(t_espacioLibre));
	new->inicioHueco = strdup(inicioHueco);
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
	new->inicioSwap = strdup(inicioSwap);
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

int tamanioRespuestaMemoria(t_respuesta_memoria unaPersona)
{
	return (sizeof(unaPersona.pid)+sizeof(unaPersona.paginas)+sizeof(unaPersona.mensajeSize));
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

	char* string = malloc(30);
	for(i=0; i<list_size(listaOcupados);i++)
	{
		newO = list_get(listaOcupados,i);

		strncpy(string,"",30);

		strncpy(string,newO->inicioSwap,4*newO->cantPag);

		printf("PID: %d\n", newO->pid);
		printf("%s\n", string);
		printf("CantPags: %d\n", newO->cantPag);

	}

	free(string);
}
t_espacioOcupado* buscarPID(int pid)
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
