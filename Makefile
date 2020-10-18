SHELL := /bin/bash
CC := gcc
CFLAGS := -I. -g -O3 -Wall

SRC := src
INCLUDE := $(SRC)/include
HS := globals.h global_errors.h deflate_errors.h aht.h h_tree.h deflate.h crc.h deflate_ext.h
OS := deflate_encode.o aht.o h_tree.o

EXEC := zencode

_HS := $(addprefix $(INCLUDE)/, $(HS))
_OS := $(addprefix $(SRC)/, $(OS))

.PHONY: clean

$(EXEC): $(_OS)
	$(CC) -o $@ $^ $(CFLAGS)

$(_OS): %.o: %.c $(_HS)
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f $(_OS)
	rm -f $(EXEC)