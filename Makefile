CC=gcc
CFLAGS=-std=gnu99 -Wall -Wextra -Werror -pedantic
LDLIBS=-lm
OUTPUT=h2o
FILES=main.c
all: main.c
	$(CC) $(CFLAGS) $(FILES) $(LDLIBS) -o $(OUTPUT) -pthread
clean:
	rm -rf $(OUTPUT)