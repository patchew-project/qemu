# SPDX-License-Identifier: GPL-2.0-or-later

CC ?= cc
CFLAGS ?=
LDFLAGS ?=
RM ?= rm -f

all: libdemo-zlib-thunk.so zcompress-demo

libdemo-zlib-thunk.so: zcompress-thunk.c zcompress-thunk.h
	$(CC) $(CFLAGS) -fPIC -shared zcompress-thunk.c -Wl,-soname,libdemo-zlib.so -o $@ $(LDFLAGS)

zcompress-demo: zcompress-demo.c zcompress-thunk.h libdemo-zlib-thunk.so
	$(CC) $(CFLAGS) zcompress-demo.c ./libdemo-zlib-thunk.so -Wl,-rpath,'$$ORIGIN' -o $@ $(LDFLAGS)

clean:
	$(RM) libdemo-zlib-thunk.so zcompress-demo

.PHONY: all clean
