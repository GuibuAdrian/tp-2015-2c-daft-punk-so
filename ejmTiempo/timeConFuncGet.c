#include <stdio.h>
#include <sys/time.h>


int main(void)
{
    struct timeval ti, tf;
    double tiempo;

    gettimeofday(&ti, NULL);   // Instante inicial
    printf("Lee este mensaje y pulsa ENTER\n");
    getchar();
    gettimeofday(&tf, NULL);   // Instante final
    tiempo= (tf.tv_sec - ti.tv_sec)*1000 + (tf.tv_usec - ti.tv_usec)/1000.0;
    printf("Has tardado: %g milisegundos\n", tiempo);
}
