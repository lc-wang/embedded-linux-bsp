CC ?= gcc

CFLAGS += -Wall -O2
CFLAGS += $(shell pkg-config --cflags libdrm)

LDFLAGS += $(shell pkg-config --libs libdrm)

all: atomic_modeset_notes

atomic_modeset_notes: atomic_modeset_notes.c
	$(CC) $(CFLAGS) -o atomic_modeset_notes \
		atomic_modeset_notes.c $(LDFLAGS)

clean:
	rm -f atomic_modeset_notes
