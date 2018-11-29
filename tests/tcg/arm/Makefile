-include ../../../config-host.mak

CROSS = arm-linux-gnu-
CC = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy

SIM = ../../../arm-softmmu/qemu-system-arm

SOURCES = $(wildcard $(SRC_PATH)/tests/tcg/arm/*.S)
BINARIES = $(patsubst %.S,%.hex,$(SOURCES))

.PHONY: clean

all: $(BINARIES)

clean:
	rm -f $(BINARIES) *.o

%.o: %.S %.ld
	$(CC) -nostdlib -Wl,--build-id=none -x assembler-with-cpp -T $(patsubst %.S,%.ld,$<) -x assembler-with-cpp -o $@ $<

%.hex: %.o
	$(OBJCOPY) -O ihex $< $@

run-test-armv6m-undef: test-armv6m-undef.hex
	$(SIM) -semihosting -M microbit -device loader,file=$<
