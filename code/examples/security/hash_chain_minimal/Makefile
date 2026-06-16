CC ?= gcc

CFLAGS += -Wall -Wextra -O2
CFLAGS += $(shell pkg-config --cflags openssl)

LDFLAGS += $(shell pkg-config --libs openssl)

all: hash_chain_minimal

hash_chain_minimal: hash_chain_minimal.c
	$(CC) $(CFLAGS) -o hash_chain_minimal \
		hash_chain_minimal.c $(LDFLAGS)

clean:
	rm -f hash_chain_minimal
