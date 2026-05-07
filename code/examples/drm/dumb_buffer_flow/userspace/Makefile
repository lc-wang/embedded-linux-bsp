CC ?= gcc

CFLAGS += -Wall -O2
CFLAGS += $(shell pkg-config --cflags libdrm)

LDFLAGS += $(shell pkg-config --libs libdrm)

all: dumb_buffer

dumb_buffer: dumb_buffer.c
	$(CC) $(CFLAGS) -o dumb_buffer dumb_buffer.c $(LDFLAGS)

clean:
	rm -f dumb_buffer
