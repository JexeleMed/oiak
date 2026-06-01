CC      = gcc
NASM    = nasm
CFLAGS  = -O2 -Wall -Wextra -std=c11
NFLAGS  = -f elf64

TARGET  = lab7
OBJS    = lab7_asm.o lab7_c.o

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

lab7_asm.o: lab7.asm
	$(NASM) $(NFLAGS) -o $@ $<

lab7_c.o: lab7.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: all
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
