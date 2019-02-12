CC=gcc
CFLAGS=-g -Wall -Wextra -pedantic -std=gnu99

.PHONY: all clean

all: shell cat hexdump mkdir ps rmdir whoami

clean:
	rm -f *.o $(PROGS)

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

shell: shell.o
	$(CC) $(CFLAGS) -o $@ $^

cat: arg.o cat.o
	$(CC) $(CFLAGS) -o $@ $^

hexdump: arg.o hexdump.o
	$(CC) $(CFLAGS) -o $@ $^

mkdir: arg.o mkdir.o
	$(CC) $(CFLAGS) -o $@ $^

ps: arg.o ps.o
	$(CC) $(CFLAGS) -o $@ $^

rmdir: arg.o rmdir.o
	$(CC) $(CFLAGS) -o $@ $^

whoami: arg.o whoami.o
	$(CC) $(CFLAGS) -o $@ $^
