all: memoria

memoria: socket.o
	gcc -c socket.c
	gcc -c memoria.c 
	gcc -o memoria memoria.o socket.o -lcommons -lpthread 
