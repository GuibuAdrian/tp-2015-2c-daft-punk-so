/*
 * procesoPlanificador.c
 *
 *  Created on: 7/9/2015
 *      Author: utnso
 */
#include <sys/socket.h>
#include <commons/config.h>

t_config *leerConfiguracion(char* direccion) {
	t_config *config;
	config = config_create(direccion);
	return config;
}
char* obtenerIP(t_config* configuracion){
	return config_get_string_value(configuracion, "IP");
}
char* obtenerPuerto(t_config* configuracion){
	return config_get_string_value(configuracion, "PUERTO_ESCUCHA");
}

int main(int argc, char *argv[]) {
	t_config configuracion = leerConfiguracion( argv[1]);
	char *ip = obtenerIP( configuracion);
	char *puerto_escucha = obtenerPuerto( configuracion);

	struct addrinfo hints;
	struct addrinfo *serverInfo;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;        // No importa si uso IPv4 o IPv6
	hints.ai_socktype = SOCK_STREAM;    // Indica que usaremos el protocolo TCP
	getaddrinfo( ip, puerto_escucha, &hints, &serverInfo);
	int listenningSocket;
	listenningSocket = socket(serverInfo->ai_family, serverInfo->ai_socktype,
			serverInfo->ai_protocol);
	bind(listenningSocket, serverInfo->ai_addr, serverInfo->ai_addrlen);
}
