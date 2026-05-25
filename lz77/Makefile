CC = gcc
CFLAGS = -Wall -Werror -O2

all: lz77

lz77: main.o lz77.o tree.o bitio.o parallel.o
	$(CC) -o lz77 main.o lz77.o tree.o bitio.o parallel.o -lm -pthread

main.o: main.c bitio.h lz77.h parallel/parallel.h
	$(CC) $(CFLAGS) -c main.c

lz77.o: lz77.c bitio.h tree.h lz77.h
	$(CC) $(CFLAGS) -c lz77.c

tree.o: tree.c tree.h
	$(CC) $(CFLAGS) -c tree.c

bitio.o: bitio.c bitio.h
	$(CC) $(CFLAGS) -c bitio.c

parallel.o: parallel/parallel.c parallel/parallel.h lz77.h bitio.h
	$(CC) $(CFLAGS) -c parallel/parallel.c -o parallel.o

.PHONY: clean

clean:
	-rm -f *.o lz77
