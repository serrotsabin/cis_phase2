# CIS Phase 1 - Makefile

CC = gcc
CFLAGS = -Wall -Wextra
LDFLAGS = -lutil

all: server client

server: server.c
	$(CC) $(CFLAGS) -o server server.c $(LDFLAGS)

client: client.c
	$(CC) $(CFLAGS) -o client client.c

clean:
	rm -f server client
	rm -rf appendix/
	rm -f /tmp/cis_test.sock

experiments: all
	@echo "Running all experiments..."
	./scripts/run_all.sh

.PHONY: all clean experiments