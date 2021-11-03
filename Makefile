CC=gcc
CFLAGS=-s -O3
all:
	$(CC) oneshot.c $(CFLAGS) -o oneshot
clean:
	rm oneshot