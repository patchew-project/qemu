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
all: a-b-kernel.h guest.h

a-b-kernel.h: hexagon.kernel
	echo "$$__note" > $@
	xxd -i $< | sed -e '/.*int.*/d' >> $@

hexagon.kernel: hexagon.elf
	$(OBJCOPY) -O binary --only-section=.text $< $@

hexagon.elf: a-b-kernel.S ../bootfile.h
	$(CC) -mcpu=hexagonv68 -nostdlib -static -fno-pie -Wl,--build-id=none \
	    -Wl,-Ttext=$(call define-value,HEXAGON_TEST_KERNEL_ADDR,../bootfile.h) \
	    -Wl,-e,_start $< -o $@

# The state guest uses the HVX and HLX extensions of the SM8975 NSP and is
# linked where state.h says the host test loads it.
guest.h: guest.bin guest.elf
	echo "$$__note" > $@
	$(NM) guest.elf | awk '$$3 == "peer_lock" \
	    { print "#define HEXAGON_PEER_LOCK_PC 0x"$$1 }' >> $@
	xxd -i guest.bin | sed -e 's/^unsigned char/static const uint8_t/' \
	    -e '/unsigned int guest_bin_len/d' >> $@

guest.bin: guest.elf
	$(OBJCOPY) -O binary --only-section=.text $< $@

guest.elf: guest.S state.h
	$(CC) -mcpu=hexagonv81 -mhvx -mhvx-length=128b -nostdlib -static \
	    -fno-pie -Wl,--build-id=none \
	    -Wl,-Ttext=$(call define-value,STATE_KERNEL_ADDR,state.h) \
	    -Wl,-e,_start $< -o $@

clean:
	$(RM) *.kernel *.elf *.bin
