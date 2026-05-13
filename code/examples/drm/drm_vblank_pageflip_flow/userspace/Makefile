CC ?= gcc

CFLAGS += -Wall -O2
CFLAGS += $(shell pkg-config --cflags libdrm)

LDFLAGS += $(shell pkg-config --libs libdrm)

all: page_flip_minimal

page_flip_minimal: page_flip_minimal.c
	$(CC) $(CFLAGS) -o page_flip_minimal \
		page_flip_minimal.c $(LDFLAGS)

clean:
	rm -f page_flip_minimal
