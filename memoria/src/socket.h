#ifndef SOCKET_H_
#define SOCKET_H_

int conectarse(char * IP, char * PUERTO_SERVIDOR);

int recibirLlamada(char * PUERTO_ESCUCHA);

int aceptarLlamada(int listenningSocket);

#endif
