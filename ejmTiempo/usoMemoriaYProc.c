/*
 ============================================================================
 Name        : usoMemoriaYProc.c
 Author      : Eri
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

/* stats.c - Muestra estadísticas del sistema respecto al uso de memoria y los procesos
 en ejecución. */
#include <stdio.h>
#include <linux/kernel.h>
//#include <linux/sys.h>
#include <sys/sysinfo.h>
int main () {
 const long minuto = 60;
 const long hora = minuto * 60;
 const long dia = hora * 24;
 const double megabyte = 1024 * 1024;
 struct sysinfo si;
 /* Obtenemos estadísticas del sistema */
 sysinfo(&si);
 /* Mostramos algunos valores interesantes contenidos en la estructura sysinfo. */

 printf("Tiempo que lleva el sist en funcionamiento: %ld dias , %ld:%02ld:%02ld\n" ,
		 (si.uptime/dia),
		 (si.uptime % dia) / hora,
		 (si.uptime % hora) / minuto,
		 si.uptime % minuto);
 printf("Memoria RAM total: %5.1f Mb\n" , si.totalram / megabyte);
 printf("Memoria RAM libre: %5.1f Mb\n" , si.freeram / megabyte);
 printf("Cantidad de procesos corriendo: %d\n" , si.procs);


 printf("Memoria RAM total: %5.1f Mb\n" , si.totalram / megabyte);
 printf("Memoria RAM libre: %5.1f Mb\n" , si.freeram / megabyte);
 printf("Cantidad de procesos corriendo: %d\n" , si.procs);
 return 0;
}
