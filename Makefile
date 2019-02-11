CC=gcc
PROGS=shell cat hexdump mkdir ps rmdir whoami
CFLAGS=-g -Wall -Wextra -pedantic -std=gnu99

.PHONY: all clean

all: $(PROGS)

clean:
	rm -f *.o $(PROGS)

%.o: %.c
	$(CC) $(CFLAGS)

%: %.o
	$(CC) $(CFLAGS)
