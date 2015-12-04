all: Planificador

Planificador: socket.o
	gcc -c socket.c
	gcc -c planificador.c 
	gcc -o Planificador planificador.o socket.o -lcommons -lpthread 
	rm -R planificador.o
	rm -R socket.o

clean:
	-$(RM) $(OBJS)$(C_DEPS)$(EXECUTABLES) Planificador
	-@echo ' '
