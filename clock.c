/*
 ============================================================================
 Name        : clock.c
 Author      : Eri
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <time.h>
#include <stdio.h>

int main()
{
   clock_t start_t, end_t, total_t;
   int i;

   start_t = clock();
   printf("Iniciando el programa, start_t = %ld\n", start_t);

   printf("Loop..., start_t = %ld\n", start_t);
   for(i=0; i< 10000000; i++)
   {
   }
   end_t = clock();
   printf("Fin de loop, end_t = %ld\n", end_t);

   //total_t = (double)(end_t - start_t)/ CLOCKS_PER_SEC;
  double difTiempo = difftime(end_t,start_t);
  printf("diferencia: %f\n", difTiempo);
  double tiempoTot = difTiempo/CLOCKS_PER_SEC;
   //printf("Tiempo total tomando por CPU: %f\n", total_t );
  printf("Tiempo total tomando por CPU: %f\n", tiempoTot);
   printf("Saliendo del programa...\n");

   return(0);
}
