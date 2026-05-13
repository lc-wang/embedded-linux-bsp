CC ?= gcc

CFLAGS += -Wall -O2
CFLAGS += $(shell pkg-config --cflags libdrm)

LDFLAGS += $(shell pkg-config --libs libdrm)

all: atomic_modeset_minimal

atomic_modeset_minimal: atomic_modeset_minimal.c
	$(CC) $(CFLAGS) -o atomic_modeset_minimal \
		atomic_modeset_minimal.c $(LDFLAGS)

clean:
	rm -f atomic_modeset_minimal
