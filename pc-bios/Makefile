#
# NOTE: only compilable with x86 cross compile tools
#
include ../config-host.mak

DEFINES=

TARGETS=

all: $(TARGETS)

%.o: %.S
	$(CC) $(DEFINES) -c -o $@ $<

%.dtb: %.dts
	dtc -I dts -O dtb -o $@ $<

vdso-linux-x64.so: vdso-linux-x64.o vdso-linux-x64.ld
	$(CC) -nostdlib -shared -Wl,-T,vdso-linux-x64.ld \
	  -Wl,-h,linux-vdso.so.1 -Wl,--hash-style=both \
	  vdso-linux-x64.o -o $@

clean:
	rm -f $(TARGETS) *.o *~
