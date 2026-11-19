# SPDX-License-Identifier: GPL-2.0-or-later

# To specify cross compiler prefix, use CROSS_PREFIX=
#   $ make CROSS_PREFIX=hexagon-unknown-linux-musl-
# The tools can also be overridden individually: CC, OBJCOPY and NM.
CC = $(or $(CROSS_PREFIX),hexagon-unknown-linux-musl-)clang
OBJCOPY = llvm-objcopy
NM = llvm-nm

# $(call define-value,NAME,header) is the value of "#define NAME" in header,
# so that load addresses are defined only once.
define-value = $(shell awk '$$2 == "$(1)" { print $$3 }' $(2))

.PHONY: all clean
all: a-b-kernel.h

a-b-kernel.h: hexagon.kernel
	echo "$$__note" > $@
	xxd -i $< | sed -e '/.*int.*/d' >> $@

hexagon.kernel: hexagon.elf
	$(OBJCOPY) -O binary --only-section=.text $< $@

hexagon.elf: a-b-kernel.S ../bootfile.h
	$(CC) -mcpu=hexagonv68 -nostdlib -static -fno-pie -Wl,--build-id=none \
	    -Wl,-Ttext=$(call define-value,HEXAGON_TEST_KERNEL_ADDR,../bootfile.h) \
	    -Wl,-e,_start $< -o $@

clean:
	$(RM) *.kernel *.elf
