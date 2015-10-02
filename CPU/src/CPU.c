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

#define BACKLOG 5			// Define cuantas conexiones vamos a mantener pendientes al mismo tiempo
#define PACKAGESIZE 1024	// Define cual va a ser el size maximo del paquete a enviar

typedef struct {
	int idNodo;
	int cantHilos;
} t_idHilo;

typedef struct {
	char *ipPlanificador;
	char *puertoPlanificador;
	char *ipMemoria;
	char *puertoMemoria;
	int numeroHilos;
} tspaghetti;

typedef struct {
	int pathSize;
	int puntero;
	char path[PACKAGESIZE];
} t_pathMensaje;

typedef struct {
	int respuestaSize;
	char respuesta[PACKAGESIZE];
} t_respuesta;

typedef struct {
	char *idProceso;
	int numeroPaginas;
} t_estructuraDeInicio;

typedef struct {
	int inicioSize;
	t_estructuraDeInicio estructuraDeInicio;
} t_mensajeDeInicio;

typedef struct {
	char *idProceso;
	int numeroDePagina;
} t_estructuraDeLectura;

typedef struct {
	int lecturaSize;
	t_estructuraDeLectura estructuraDeLectura;
} t_mensajeDeLectura;

typedef struct {
	char *idProceso;
} t_estructuraDeFinalizacion;

typedef struct {
	int finalizacionSize;
	t_estructuraDeFinalizacion estructuraDeFinalizacion;
} t_mensajeDeInicio;

int id1 = 5;
int RETARDO, socketMemoria;

int tamanioEstructura2(t_pathMensaje pathDeMensaje);
int tamanioRespuesta(t_respuesta unaRespuesta);
void conectarHilos();
void recibirPath(int serverSocket);
char * obtenerLinea(char path[PACKAGESIZE], int puntero);
void enviarRespuesta(int socketPlanificador);
void enviarAMemoria(char * linea);

int main() {
	printf("\n");
	printf("----CPU----\n\n");

	t_config* config = config_create(
			"/home/utnso/github/tp-2015-2c-daft-punk-so/CPU/config.cfg");
	tspaghetti spaghetti;

	spaghetti.ipPlanificador = config_get_string_value(config,
			"IP_PLANIFICADOR");
	spaghetti.puertoPlanificador = config_get_string_value(config,
			"PUERTO_PLANIFICADOR");
	spaghetti.ipMemoria = config_get_string_value(config, "IP_MEMORIA");
	spaghetti.puertoMemoria = config_get_string_value(config, "PUERTO_MEMORIA");
	spaghetti.numeroHilos = config_get_int_value(config, "CANTIDAD_HILOS");
	RETARDO = config_get_int_value(config, "RETARDO");

	int i = 1;

	while (i <= spaghetti.numeroHilos) {
		pthread_t unHilo;

		pthread_create(&unHilo, NULL, (void*) conectarHilos, &spaghetti);
		pthread_join(unHilo, NULL);
		i++;
	}

	close(socketMemoria);
	return 0;
}

void conectarHilos(void *context) {
	tspaghetti *spaghetti = context;
	int serverSocket;
	serverSocket = conectarse( spaghetti->ipPlanificador, spaghetti->puertoPlanificador);
	socketMemoria = conectarse( spaghetti->ipMemoria, spaghetti->puertoMemoria);
	t_idHilo mensaje;

	id1++;
	mensaje.idNodo = id1;
	mensaje.cantHilos = spaghetti->numeroHilos;

	//Envio id y aviso la cantidad de conexiones
	void* package = malloc(sizeof(t_idHilo));

	memcpy(package, &mensaje.idNodo, sizeof(mensaje.idNodo));
	memcpy(package + sizeof(mensaje.idNodo), &mensaje.cantHilos,
			sizeof(mensaje.cantHilos));

	send(serverSocket, package, sizeof(t_idHilo), 0);
	free(package);
	recibirPath(serverSocket);
	close(serverSocket);
}

void recibirPath(int serverSocket) {
	int status = 1;
	t_pathMensaje pathDeMensaje;
	void* package = malloc(tamanioEstructura2(pathDeMensaje));

	while (status) {
		status = recv(serverSocket, (void*) package,
				sizeof(pathDeMensaje.puntero), 0);

		if (status == 0) {
			break;
		} else {
			memcpy(&pathDeMensaje.puntero, package,
					sizeof(pathDeMensaje.puntero));
			recv(serverSocket,
					(void*) (package + sizeof(pathDeMensaje.puntero)),
					sizeof(pathDeMensaje.pathSize), 0);
			memcpy(&pathDeMensaje.pathSize,
					package + sizeof(pathDeMensaje.puntero),
					sizeof(pathDeMensaje.pathSize)); //--

			void* package2 = malloc(pathDeMensaje.pathSize);

			recv(serverSocket, (void*) package2, pathDeMensaje.pathSize, 0);
			memcpy(&pathDeMensaje.path, package2, pathDeMensaje.pathSize);
			char * linea = obtenerLinea(pathDeMensaje.path,
					pathDeMensaje.puntero);

			enviarAMemoria(linea);
			enviarRespuesta(serverSocket);

			sleep(RETARDO);
			free(linea);
			free(package2);
		}

	}

	free(package);
}

void enviarRespuesta(int socketPlanificador) {
	t_respuesta respuesta;

	strcpy(respuesta.respuesta, "Exito :D\n");
	respuesta.respuestaSize = strlen(respuesta.respuesta) + 1;

	void* respPackage = malloc(tamanioRespuesta(respuesta));

	memcpy(respPackage, &respuesta.respuestaSize,
			sizeof(respuesta.respuestaSize));
	memcpy(respPackage + sizeof(respuesta.respuestaSize), respuesta.respuesta,
			respuesta.respuestaSize);

	send(socketPlanificador, respPackage, tamanioRespuesta(respuesta), 0);

	free(respPackage);
}

char * obtenerLinea(char path[PACKAGESIZE], int puntero) {
	FILE* file;
	file = txt_open_for_read(path);

	printf("Linea: %d\n", puntero);
	char * linea = read_line(file, puntero - 1);
	txt_close_file(file);

	return linea;
}

void enviarAMemoria(char * linea) {
	t_respuesta respuesta;
	strcpy(respuesta.respuesta, linea);

	//TODO interpretar la respuesta no pasar directamente la linea a la memoria :S
	interprete(linea); // aca se va a enviar mensaje a la memoria dependiendo de la
					   // instruccion que leamos del mCod

//	respuesta.respuestaSize = strlen(respuesta.respuesta) + 1;
//	void* respPackage = malloc(tamanioRespuesta(respuesta));
//
//	memcpy(respPackage, &respuesta.respuestaSize,
//			sizeof(respuesta.respuestaSize));
//	memcpy(respPackage + sizeof(respuesta.respuestaSize), respuesta.respuesta,
//			respuesta.respuestaSize);
//
//	send(socketMemoria, respPackage, tamanioRespuesta(respuesta), 0);
//	free(respPackage);
}

int tamanioEstructura2(t_pathMensaje pathDeMensaje) {
	return (sizeof(pathDeMensaje.puntero) + sizeof(pathDeMensaje.pathSize)
			+ strlen(pathDeMensaje.path));
}

int tamanioRespuesta(t_respuesta unaRespuesta) {
	return (sizeof(unaRespuesta.respuestaSize) + unaRespuesta.respuestaSize);
}

void interprete(char *linea) {
	char *instruccion;
	t_estructuraDeInicio estructuraDeInicio;
	t_estructuraDeLectura estructuraDeLectura;
	t_estructuraDeFinalizacion estructuraDeFinzalizacion;

	instruccion = buscarInstruccion(linea);
	if (instruccion == "iniciar") {
		estructuraDeInicio.idProceso;
		estructuraDeInicio.numeroPaginas = valorAsociado(linea);
		//TODO mensaje de inicio
		iniciarProceso(estructuraDeInicio);
	} else {
		if (instruccion == "leer") {
			estructuraDeLectura.idProceso;
			estructuraDeLectura.numeroDePagina = valorAsociado(linea);
			//TODO mensaje de lectura
			leerPagina(estructuraDeLectura);
		} else {
			if (instruccion == "finalizar") {
				estructuraDeFinzalizacion.idProceso;
				//TODO mensaje de finalizacion
				finalizarProceso(estructuraDeFinzalizacion);
			}
		}
	}
}

char *buscarInstruccion(char *linea) {
	int i = 0;
	char *instruccion;

	while (linea[i] != 32) {
		instruccion[i] = linea[i];
		i++;
	}

	instruccion[i + 1] = "\0";
	return instruccion;
}

int valorAsociado(char *linea) {
	int i = 0, j = 0;
	int valor;
	char *valorString;

	while (linea[i] != 32) {
		i++;
	}
	while (linea[i] != 32) {
		valorString[j] = linea[i];
		j++;
		i++;
	}

	valorString[j + 1] = "\0";
	valor = atoi(valorString);
	return valor;
}

char *obtenerTexto(char *linea) {
	char *texto;
	int i = 0, j = 0, cantidadEspacios = 0;

	while (linea[i] != 32 || cantidadEspacios < 2) {
		if (linea[i] == 32)
			cantidadEspacios++;
		i++;
	}
	while (linea != "\0") {
		texto[j] = linea[i];
		j++;
		i++;
	}

	texto[j + 1] = "\0";
	return texto;
}
//TODO similar para Leer y Finalizar
void iniciarProceso(t_estructuraDeInicio estructuraDeInicio) {
	t_mensajeDeInicio mensajeDeInicio;
	mensajeDeInicio.inicioSize = tamanioEstructuraDeInicio(estructuraDeInicio);
	mensajeDeInicio.estructuraDeInicio = estructuraDeInicio;
	void* respPackage = malloc(tamanioMensajeDeInicio(mensajeDeInicio));

	memcpy(respPackage, &mensajeDeInicio.inicioSize,sizeof(mensajeDeInicio.inicioSize));
	memcpy(respPackage + sizeof(mensajeDeInicio.inicioSize), mensajeDeInicio.estructuraDeInicio, mensajeDeInicio.inicioSize);

	send(socketMemoria, respPackage, tamanioMensajeDeInicio(mensajeDeInicio), 0);
	free(respPackage);
}

int tamanioMensajeDeInicio(t_mensajeDeInicio mensajeDeInicio){
	return sizeof(mensajeDeInicio.inicioSize) + tamanioEstructuraDeInicio(mensajeDeInicio.estructuraDeInicio);
}
int tamanioEstructuraDeInicio(t_estructuraDeInicio estructuraDeInicio){
	return strlen(estructuraDeInicio.idProceso) + sizeof(estructuraDeInicio.numeroPaginas);
}




