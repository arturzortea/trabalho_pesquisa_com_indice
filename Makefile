CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -g
# main.c inclui os demais .c (compilação em um arquivo só); por isso só ele é compilado
SRC     = main.c tabela.c modelos.c consultas.c arvoreb.c hash.c
HDR     = tabela.h modelos.h consultas.h arvoreb.h hash.h

trabalho1: $(SRC) $(HDR)
	$(CC) $(CFLAGS) main.c -o trabalho1

clean:
	rm -f trabalho1 trabalho1.exe *.tmp
