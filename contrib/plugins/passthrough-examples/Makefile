# SPDX-License-Identifier: GPL-2.0-or-later

GUEST_CC ?= cc
GUEST_CPPFLAGS ?=
GUEST_CFLAGS ?=
GUEST_LDFLAGS ?=
RM ?= rm -f

all: libz.so zlib-demo

libz.so: zlib-gtl.c passthrough-gtl.c passthrough-gtl.h ../passthrough-protocol.h
	$(GUEST_CC) $(GUEST_CPPFLAGS) $(GUEST_CFLAGS) -fPIC -shared \
		zlib-gtl.c passthrough-gtl.c -Wl,-soname,libz.so \
		-o $@ $(GUEST_LDFLAGS)

zlib-demo: zlib-demo.c libz.so
	$(GUEST_CC) $(GUEST_CPPFLAGS) $(GUEST_CFLAGS) zlib-demo.c -L. -lz \
		-o $@ $(GUEST_LDFLAGS)

clean:
	$(RM) libz.so zlib-demo

.PHONY: all clean
