.PHONY: all clean

all: nyush

nyush: nyush.c
	gcc -std=c17 -Wall -Werror -pedantic -o nyush nyush.c

clean:
	rm -f nyush
