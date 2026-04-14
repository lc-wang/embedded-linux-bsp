CC ?= gcc
CFLAGS = -Wall -O2

all: fault_user

fault_user: fault_user.c
	$(CC) $(CFLAGS) -o fault_user fault_user.c

clean:
	rm -f fault_user
