SHELL := /bin/bash
CC := gcc
CFLAGS = -I. -Wall -g -D _DEBUG

SRC := src
INCLUDE := $(SRC)/include
HS := globals.h global_errors.h deflate_errors.h aht.h h_tree.h deflate.h crc.h deflate_ext.h
OS := error_checkpoint.o deflate_compress.o deflate_decompress.o aht.o h_tree.o

UTILSRC := util/src
UTILBIN := util/bin

EXEC := zencode

_HS := $(addprefix $(INCLUDE)/, $(HS))
_OS := $(addprefix $(SRC)/, $(OS))

.PHONY: clean do_debug debug

$(EXEC): $(_OS)
	$(CC) -o $@ $^ $(CFLAGS)

$(_OS): %.o: %.c $(_HS)
	$(CC) -c -o $@ $< $(CFLAGS)

check_lld: $(_OS) tests/check_lld.c
	$(CC) -o $@ $^ $(CFLAGS)

util: $(UTILSRC)/*.c
	@for file in $^; do \
		$(CC) -o ${file/$(UTILSRC)/$(UTILBIN)} $file
	done

clean:
	rm -f $(_OS)
	rm -f $(EXEC)