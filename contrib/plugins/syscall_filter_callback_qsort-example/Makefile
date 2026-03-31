# SPDX-License-Identifier: GPL-2.0-or-later

CC ?= cc
CFLAGS ?=
LDFLAGS ?=
RM ?= rm -f

all: libdemo-callback-qsort-thunk.so callback-demo

libdemo-callback-qsort-thunk.so: callback-thunk.S
	$(CC) $(CFLAGS) -fPIC -shared callback-thunk.S -Wl,-soname,libdemo-callback-qsort.so -o $@ $(LDFLAGS)

callback-demo: callback-demo.c callback-qsort.h libdemo-callback-qsort-thunk.so
	$(CC) $(CFLAGS) callback-demo.c ./libdemo-callback-qsort-thunk.so -Wl,-rpath,'$$ORIGIN' -o $@ $(LDFLAGS)

clean:
	$(RM) libdemo-callback-qsort-thunk.so callback-demo

.PHONY: all clean
