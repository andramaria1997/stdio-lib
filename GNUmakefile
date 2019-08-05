CC = gcc
CFLAGS = -Wall -fPIC

all: build

build: libso_stdio.o
	$(CC) -shared libso_stdio.o -o libso_stdio.so

libso_stdio.o: libso_stdio.c
	$(CC) $(CFLAGS) -c libso_stdio.c

clean:
	rm -f *.o libso_stdio.so
