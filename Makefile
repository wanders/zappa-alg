# -*- makefile-gmake -*-

.PHONY: all
all: zappa-alg

CFLAGS=-std=gnu99 -Wall -Wextra 

zappa-alg: zappa-alg.c insane-macros.h
	$(CC) -o $@ $<



.PHONY: install
install: zappa-alg
	scp zappa-alg root@openwrt: && ssh -t root@openwrt ./zappa-alg $(ZAPPA_ARGS)

.PHONY: clean
clean:
	rm -f zappa-alg

-include Makefile.local
