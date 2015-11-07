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
#define MAXCHARCOMMAND 50
#define MAXCHARPARAMETERS 200
#define NETWORKMODE 0
#define CONSOLEMODE 1

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
int tamanio, cantPagSwap, tamanioPagSwap, consoleMode;
int socket_memoria;

int recvall(int s, void *toReceive, int size, int flags); // Función segura para recibir datos, se asegura de que recvall() reciba TODO (hay casos en los que, por detalles de bajo nivel, recvall() no recibe todo lo que debía recibir, es por eso que devuelve la cantidad de bytes recibidos)
void parseConsoleCommand(char *commandLine,char *command,char *arguments);
void simularPedidoMemoria(char * arguments);
static t_espacioLibre *libre_create(char* inicioHueco, int cantPag);
static void libre_destroy(t_espacioLibre *self);
static t_espacioOcupado *ocupado_create(int pid, char* inicioSwap, int cantPag);
static void ocupado_destroy(t_espacioOcupado *self);
int tamanio_archivo(int fd);
int tamanioRespuestaMemoria(t_orden_memoria unaPersona);

char* mapearArchivo();
void crearArchivoSwap(char * nombreSwap, int tamanioSwap, int cantSwap);
void procesarOrden(t_orden_memoria ordenMemoria, int consoleMode);
void mostrarListas();
int encontrarPosicionOcupado(int pid);
int encontrarPosicionEspacioLibre(char* inicioHueco);
t_espacioOcupado* buscarPIDEnOcupados(int pid);
t_espacioLibre* buscarEspacioAOcupar(int cantPags);


int main()
{
	printf("\n");
	printf("~~~~~~~~~~SWAP~~~~~~~~~~\n\n");


	logger = log_create("logsTP", "Swap", true, LOG_LEVEL_INFO);

	t_config* config;

	config = config_create("config.cfg");
	char *nombreSwap = config_get_string_value( config, "NOMBRE_SWAP");
	cantPagSwap = config_get_int_value( config, "CANTIDAD_PAGINAS");
	tamanioPagSwap = config_get_int_value( config, "TAMANIO_PAGINA");
	char * PUERTO_ESCUCHA = config_get_string_value(config, "PUERTO_ESCUCHA");
	consoleMode = config_get_int_value(config, "CONSOLE_MODE");


	listaLibres = list_create();
	listaOcupados = list_create();


	crearArchivoSwap(nombreSwap, tamanioPagSwap, cantPagSwap);
	printf("\n");

	char* mapeo = mapearArchivo();


	list_add(listaLibres, libre_create(mapeo, cantPagSwap));


	if(consoleMode == 0) {	// Recibo comandos desde Memoria desde la red

		int listenningSocket = recibirLlamada(PUERTO_ESCUCHA);
		socket_memoria = aceptarLlamada(listenningSocket);


		log_info(logger, "Memoria conectado");


		int result = 1;

		while(result)
		{
			t_orden_memoria ordenMemoria;
			void* package = malloc(sizeof(int)*4);

			result = recvall(socket_memoria, (void*)package, sizeof(ordenMemoria.pid), 0);
			if(result < 0)
			{
				break;
			}
			memcpy(&ordenMemoria.pid,package,sizeof(ordenMemoria.pid));
			recvall(socket_memoria,(void*) (package+sizeof(ordenMemoria.pid)), sizeof(ordenMemoria.paginas), 0);
			memcpy(&ordenMemoria.orden, package+sizeof(ordenMemoria.pid),sizeof(ordenMemoria.orden));
			recvall(socket_memoria,(void*) (package+sizeof(ordenMemoria.pid)+sizeof(ordenMemoria.orden)), sizeof(ordenMemoria.paginas), 0);
			memcpy(&ordenMemoria.paginas, package+sizeof(ordenMemoria.pid)+sizeof(ordenMemoria.orden),sizeof(ordenMemoria.paginas));
			recvall(socket_memoria,(void*) (package+sizeof(ordenMemoria.pid)+sizeof(ordenMemoria.orden)+sizeof(ordenMemoria.paginas)), sizeof(ordenMemoria.contentSize), 0);
			memcpy(&ordenMemoria.contentSize, package+sizeof(ordenMemoria.pid)+sizeof(ordenMemoria.orden)+sizeof(ordenMemoria.paginas), sizeof(ordenMemoria.contentSize));


			void* package2=malloc(ordenMemoria.contentSize);

			recvall(socket_memoria,(void*) package2, ordenMemoria.contentSize, 0); //campo longitud(NO SIZEOF DE LONGITUD)
			memcpy(&ordenMemoria.content, package2, ordenMemoria.contentSize);


			procesarOrden(ordenMemoria, NETWORKMODE);

			free(package);
			free(package2);
		}
		close(listenningSocket);
		close(socket_memoria);

	} else {
			// Empieza el comportamiento de la consola

			char *commandLine = malloc(MAXCHARCOMMAND + MAXCHARPARAMETERS);
			char *command = malloc(MAXCHARCOMMAND);
			char *arguments = malloc(MAXCHARPARAMETERS);

			printf("=================================CONSOLA=================================\n\nComandos soportados: \n--------------------\n\nsimularPedidoMemoria(pid,orden,paginas,paquete)\ndumpSwap()\nshowConfig()\nsalir()\n\nNOTA: Recordar que las ordenes disponibles son  0=Iniciar, 1=Leer, 2=Escribir, 3=Finalizar\nNOTA: En el caso de la simulacion, el paquete termina con \\0 ya que se simula usando un string\n\nEsperando comandos\n");

			// Espera un comando por siempre

			while (1) {

				//Parsea el comando recibido:

				parseConsoleCommand(commandLine,command,arguments);
				printf("--------------------------------------\n\n");

				// Para cada comando, se asigna una acción

				if( strncmp( command , "simularPedidoMemoria", MAXCHARCOMMAND) == 0) {

					simularPedidoMemoria(arguments);

				} else

				if( strncmp( command , "dumpSwap", MAXCHARCOMMAND) == 0) {
					printf(" Recibi %s", command);
				} else

				if( strncmp( command , "showConfig", MAXCHARCOMMAND) == 0) {
					printf(" Recibi %s", command);
				} else

				if( strncmp( command , "salir", MAXCHARCOMMAND) == 0) {
					break;
				} else

				{
					printf("Comando no reconocido\n");
				}
				printf("--------------------------------------\n");

			}
	}


	list_destroy_and_destroy_elements(listaOcupados,(void*) ocupado_destroy);
	list_destroy_and_destroy_elements(listaLibres,(void*) libre_destroy);


    munmap( mapeo, tamanio );

    config_destroy(config);
    log_info(logger, "---------------------FIN---------------------");
    log_destroy(logger);

    return 0;
}

void simularPedidoMemoria(char * arguments) {

	t_orden_memoria ordenMemoria;
	char caracterParametro[PACKAGESIZE];

	int argumentCounter = 0;
	int i=0;
	int j=0;

	//PID
	while((arguments[i] != ',') && (arguments[i] != '\0')) {
		caracterParametro[j] = arguments[i];
		j++;
		i++;
	}
	caracterParametro[j] = '\0';
	ordenMemoria.pid = atoi(caracterParametro);
	i++;
	j=0;

	//ORDEN
	while((arguments[i] != ',') && (arguments[i] != '\0')) {
		caracterParametro[j] = arguments[i];
		j++;
		i++;
	}
	caracterParametro[j] = '\0';
	ordenMemoria.orden = atoi(caracterParametro);
	i++;
	j=0;

	//PAGINAS
	while((arguments[i] != ',') && (arguments[i] != '\0')) {
		caracterParametro[j] = arguments[i];
		j++;
		i++;
	}
	caracterParametro[j] = '\0';
	ordenMemoria.paginas = atoi(caracterParametro);
	i++;
	j=0;

	//payload
	while((arguments[i] != ',') && (arguments[i] != '\0')) {
		caracterParametro[j] = arguments[i];
		j++;
		i++;
	}
	caracterParametro[j] = '\0';
	strcpy(ordenMemoria.content, caracterParametro);
	i++;
	j=0;

	ordenMemoria.contentSize = strlen(ordenMemoria.content);

	//printf("el pid es %d, el orden es %d, las paginas son %d, el length es %d, y el payload es %s\n\n", ordenMemoria.pid , ordenMemoria.orden, ordenMemoria.paginas, ordenMemoria.contentSize, ordenMemoria.content);
	procesarOrden(ordenMemoria, CONSOLEMODE);

}

void parseConsoleCommand(char *commandLine,char *command,char *arguments) {
            int i = 0;
            int j = 0;

            scanf("%s",commandLine); // hacerTalCosa(param1,param2,param3)

            while ( commandLine[i] != '(' && commandLine[i]!='\0') {
                    command[i] = commandLine[i];
                    i++;
            }
            command[i] = '\0';
            if(commandLine[i] !='\0'){

                    i++;
                    while ( commandLine[i] != ')' && commandLine[i] !='\0' ) {
                            arguments[j] = commandLine[i];
                            i++;
                            j++;
                    }
            }
            //printf("Comando: (%s). Parametro: (%s)\n", command, arguments);
            arguments[j] = '\0';

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

void finalizarProceso(int pid, int pag)
{
	log_info(logger, "Finalizando mProc: %d", pid);

	respuestaMemoria(pid, pag, 3, "/");

	int posOcupado = encontrarPosicionOcupado(pid);

	if(posOcupado != -1)	//Pregunto si lo encontre
	{
		t_espacioOcupado *ocupadoAux = list_remove(listaOcupados, posOcupado);

		list_add(listaLibres, libre_create(ocupadoAux->inicioSwap, ocupadoAux->cantPag));
	}

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

void procesarOrden(t_orden_memoria ordenMemoria, int mode )
{
	int respuesta;

	if (ordenMemoria.orden == 0)  // 0=Iniciar
	{
		log_info(logger, "Iniciando mProc: %d de %d paginas", ordenMemoria.pid, ordenMemoria.paginas);

		respuesta = reservarEspacio(ordenMemoria.pid, ordenMemoria.paginas);

		if(mode == CONSOLEMODE) {

			respuesta?printf("Fallo inicio PID %d", ordenMemoria.pid):printf("Inicio exitoso PID %d", ordenMemoria.pid);

		} else {
			if (respuesta)  // 0 = Exito, 1 = Fallo
			{
				respuestaMemoria(ordenMemoria.pid, ordenMemoria.paginas, 1, "/");
			}
			else
			{
				respuestaMemoria(ordenMemoria.pid, ordenMemoria.paginas, 0, "/");
			}
		}

	}
	else
	{
		if (ordenMemoria.orden == 1) // 1=Leer
		{

			t_espacioOcupado* pidOcup = buscarPIDEnOcupados(ordenMemoria.pid);
			char * pagContent = malloc(20);

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

				finalizarProceso(ordenMemoria.pid, ordenMemoria.paginas);
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

	//char* nombre_archivo = "/home/utnso/github/tp-2015-2c-daft-punk-so/Swap/Debug/swap.data";
	char* nombre_archivo = "swap.data";

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

void crearArchivoSwap(char * nombreDelArchivo, int tamanioPagina, int cantidadDePaginas)
{
  char buffer[255];
  sprintf (buffer,"dd if=/dev/zero of=%s bs=%d count=%d", nombreDelArchivo, tamanioPagina, cantidadDePaginas);
  system(buffer);
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
int encontrarPosicionOcupado(int pid)
{
	t_espacioOcupado* new;

	int i=0;
	int encontrado = -1;

	while( (i<list_size(listaOcupados)) && encontrado!=0)
	{
		new = list_get(listaOcupados,i);

		if(new->pid == pid)
		{
			encontrado = 0;
		}
		else
		{
			i++;
		}
	}

	if(encontrado == 0)
	{
		return i;
	}

	return encontrado;
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

int recvall(int s, void *toReceive, int size, int flags) {
	unsigned char *buffer = (unsigned char*) toReceive;
    int ret;
	int bytesleft = size;

	// Cicla hasta que reciba TODO
    while(bytesleft>0) {
        ret = recv(s,buffer,bytesleft,flags);
        if ((ret == -1) || (ret == 0)) { return ret; }
		buffer += ret;
        bytesleft -= ret;
    }

    return size;
}
