CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -g
SRC     = main.c tabela.c modelos.c consultas.c
HDR     = tabela.h modelos.h consultas.h

trabalho1: $(SRC) $(HDR)
	$(CC) $(CFLAGS) $(SRC) -o trabalho1

clean:
	rm -f trabalho1 trabalho1.exe *.tmp
