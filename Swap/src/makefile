all: Swap

Swap: socket.o
	gcc -c socket.c
	gcc -c swap.c 
	gcc -o Swap swap.o socket.o -lcommons -lpthread 
	rm -R swap.o
	rm -R socket.o

clean:
	rm -R Swap
	