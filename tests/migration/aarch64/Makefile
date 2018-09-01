# To specify cross compiler prefix, use CROSS_PREFIX=
#   > make CROSS_PREFIX=aarch64-linux-gnu-

.PHONY: all clean
all: aarch64-a-b-kernel.h

aarch64-a-b-kernel.h: aarch64.kernel
	echo "$$__note" > header.tmp
	xxd -i $< | sed -e 's/.*int.*//' >> header.tmp
	mv header.tmp $@

aarch64.kernel: aarch64.elf
	$(CROSS_PREFIX)objcopy -O binary $< $@

aarch64.elf: aarch64-a-b-kernel.S
	$(CROSS_PREFIX)gcc -o $@ -nostdlib -Wl,--build-id=none $<

clean:
	@rm -rf *.kernel *.elf

