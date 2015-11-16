/*
 ============================================================================
 Name        : fechaActual.c
 Author      : Eri
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

/* mi_date.c - Muestra la fecha y la hora actual por pantalla */
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>


void print_time();

int main() {
 print_time();
 return 0;
}


void print_time () {
 struct timeval tv;
 struct tm* ptm;
 char time_string[40];
 /*fecha y hora del día y se transforma en un estructura tm*/
 gettimeofday(&tv, NULL);
 ptm = localtime(&tv.tv_sec);
 /*Utilizando la estructura tm se crea un string con la informacion que se quiere*/
 strftime(time_string, sizeof(time_string), "%d/%m/%Y %H:%M:%S" , ptm);
 printf( "%s\n" ,time_string);
}
