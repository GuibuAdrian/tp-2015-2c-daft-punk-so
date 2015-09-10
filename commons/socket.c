#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include "socket.h"

#define BACKLOG 5


int conectarse(char * IP, char * PUERTO_SERVIDOR)
{
	struct addrinfo hints;
	struct addrinfo *serverInfo;

	memset(&hints, 0, sizeof(hints));

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;


	getaddrinfo(IP, PUERTO_SERVIDOR, &hints, &serverInfo);


	int socket_servidor;
	socket_servidor = socket(serverInfo->ai_family, serverInfo->ai_socktype, serverInfo->ai_protocol);


	connect(socket_servidor, serverInfo->ai_addr, serverInfo->ai_addrlen);
	freeaddrinfo(serverInfo);

	return socket_servidor;
};



int recibirLlamada(char * PUERTO_ESCUCHA)
{
	struct addrinfo hints;
	struct addrinfo *serverInfo;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_socktype = SOCK_STREAM;

	getaddrinfo(NULL, PUERTO_ESCUCHA, &hints, &serverInfo);


	int listenningSocket;
	listenningSocket = socket(serverInfo->ai_family, serverInfo->ai_socktype, serverInfo->ai_protocol);


	bind(listenningSocket,serverInfo->ai_addr, serverInfo->ai_addrlen);
	freeaddrinfo(serverInfo);


	listen(listenningSocket, BACKLOG);

	return(listenningSocket);
};


int aceptarLlamada(int listenningSocket)
{
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);

	return accept(listenningSocket, (struct sockaddr *) &addr, &addrlen);
};
