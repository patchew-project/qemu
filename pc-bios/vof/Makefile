all: build-all

build-all: vof.bin

%.o: %.S
	cc -m32 -mbig-endian -c -o $@ $<

%.o: %.c
	cc -m32 -mbig-endian -c -fno-stack-protector -o $@ $<

vof.elf: entry.o main.o ci.o bootmem.o libc.o
	ld -nostdlib -e_start -Tl.lds -EB -o $@ $^

%.bin: %.elf
	objcopy -O binary -j .text -j .data -j .toc -j .got2 $^ $@

clean:
	rm -f *.o vof.bin vof.elf *~
