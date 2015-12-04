all: CPU

CPU: socket.o txt.o
	gcc -c socket.c
	gcc -c txt.c
	gcc -c CPU.c 
	gcc -o CPU CPU.o socket.o txt.o -lcommons -lpthread 

