CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -pthread

all: scheduler

scheduler: shell.c scheduler.c scheduler.h
	$(CC) $(CFLAGS) shell.c scheduler.c -o scheduler

clean:
	rm -f scheduler scheduler.o
