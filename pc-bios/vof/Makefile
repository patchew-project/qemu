all: build-all

build-all: vof.bin

%.o: %.S
	cc -m32 -mbig-endian -c -o $@ $<

%.o: %.c
	cc -m32 -mbig-endian -c -fno-stack-protector -Wno-builtin-declaration-mismatch -o $@ $<

vof.elf: entry.o main.o libc.o ci.o bootmem.o
	ld -nostdlib -e_start -Tl.lds -EB -o $@ $^

%.bin: %.elf
	objcopy -O binary -j .text -j .data -j .toc -j .got2 $^ $@

clean:
	rm -f *.o *.bin *.elf *~
