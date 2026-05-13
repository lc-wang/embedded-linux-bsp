CC ?= gcc

CFLAGS += -Wall -O2
CFLAGS += $(shell pkg-config --cflags libdrm)

LDFLAGS += $(shell pkg-config --libs libdrm)

all: prime_fd_notes

prime_fd_notes: prime_fd_notes.c
	$(CC) $(CFLAGS) -o prime_fd_notes prime_fd_notes.c $(LDFLAGS)

clean:
	rm -f prime_fd_notes
