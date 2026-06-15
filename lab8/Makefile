CC     = gcc
CFLAGS = -O2 -Wall -Wextra

all: integrate

integrate: main.c simd.o
	$(CC) $(CFLAGS) main.c simd.o -o integrate -lm

simd.o: simd.asm
	nasm -f elf64 simd.asm -o simd.o

clean:
	rm -f integrate simd.o

.PHONY: all clean
