SHELL := /bin/bash
CC := gcc
CFLAGS := -I. -g -O3 -Wall

INCLUDE := include
HS := globals.h errors.h deflate_errors.h aht.h h_tree.h deflate.h crc.h deflate_ext.h
OS := deflate_encode.o aht.o h_tree.o

EXEC := zencode

.PHONY: clean

$(EXEC): $(OS)
	$(CC) -o $@ $^ $(CFLAGS)

$(OS): %.o: %.c $(HS)
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f $(OS)
	rm -f $(EXEC)