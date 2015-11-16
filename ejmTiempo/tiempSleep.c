#include <stdio.h>
#include <time.h>

int main ()
{

   time_t start_t, end_t;
   double diff_t;

   printf("Iniciando el programa...\n");
   time(&start_t);

  printf("Esperando el programa durante 5 seg...\n");
  sleep(5);

  time(&end_t);
  diff_t = difftime(end_t, start_t);
  printf("Tiempo de ejecución = %f\n", diff_t);
  printf("Saliendo del programa...\n");

  return(0);
}


