CC ?= gcc

CFLAGS += -Wall -O2
CFLAGS += $(shell pkg-config --cflags libdrm)

LDFLAGS += $(shell pkg-config --libs libdrm)

all: modeset_minimal

modeset_minimal: modeset_minimal.c
	$(CC) $(CFLAGS) -o modeset_minimal modeset_minimal.c $(LDFLAGS)

clean:
	rm -f modeset_minimal
