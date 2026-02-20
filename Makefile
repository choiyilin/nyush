CC=gcc
CFLAGS=-g -pedantic -std=gnu17 -Wall -Werror -Wextra

.PHONY: all
all: nyush

nyush: nyush.c
	$(CC) $(CFLAGS) -o nyush nyush.c

.PHONY: clean
clean:
	rm -f *.o nyush
