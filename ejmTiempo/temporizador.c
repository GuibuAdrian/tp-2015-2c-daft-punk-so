/*
 ============================================================================
 Name        : temporizador.c
 Author      : Eri
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/time.h>


void uswtime(double *, double *, double *);


int main(){

	double utime0, stime0, wtime0,utime1, stime1, wtime1;
	register int i,j,k,a;

/* ---- Primera invocación a la función. ---- */
	uswtime(&utime0, &stime0, &wtime0);

/* ---- Segmento de código cuyo tiempo de ejecución se desea medir. --- */
	for(i=0; i<10000; i++)
		for(j=0; j<100; j++)
			for(k=0; k<100; k++)
				a++;

/* ---- Segunda invocación a la función. ---- */
	uswtime(&utime1, &stime1, &wtime1);

/* --- Cálculo del tiempo de ejecución e impresión de resultados. --- */
	printf("real    %.3f\n",  wtime1 - wtime0);
	printf("usuario %.3f\n",  utime1 - utime0);
	printf("sistema %.3f\n",  stime1 - stime0);
	printf("\n");
	printf("CPU/Wall   %.3f %% \n",100.0 * (utime1 - utime0 + stime1 - stime0) / (wtime1 - wtime0));
return 0;
}

/*
 * Tiempo real (tambiénn wall time): lo que tarda desde que le lanzó el programa a ejecutarse hasta que el prog finalizó y da los resultados
 * Tiempo usuario: tiempo que el CPU le dedicó exclusivamente al programa
 * Tiempo sistema: tiempo que el CPU se dedicó a dar servicio al SO por necesidades del programa
 * */



void uswtime(double *usertime, double *systime, double *walltime)
{
	double mega = 1.0e-6;
	struct rusage buffer;
	struct timeval tp;
	struct timezone tzp;

	getrusage(RUSAGE_SELF, &buffer);
	gettimeofday(&tp, &tzp);

	*usertime = (double) buffer.ru_utime.tv_sec + 1.0e-6 * buffer.ru_utime.tv_usec;
	*systime  = (double) buffer.ru_stime.tv_sec + 1.0e-6 * buffer.ru_stime.tv_usec;
	*walltime = (double) tp.tv_sec + 1.0e-6 * tp.tv_usec;
}


