CC = gcc
CFLAGS = -Wall -Wextra -Isrc
LDFLAGS = -lutil

all: server client

server: src/server_v2.c src/protocol.h
	$(CC) $(CFLAGS) -o server src/server_v2.c $(LDFLAGS)

client: src/client_v2.c
	$(CC) $(CFLAGS) -o client src/client_v2.c

clean:
	rm -f server client /tmp/cis.sock

test: all
	@echo "Run './server' in one terminal"
	@echo "Run './client' in 3 other terminals"

.PHONY: all clean test