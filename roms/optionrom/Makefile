all: multiboot_dma.bin linuxboot_dma.bin kvmvapic.bin pvh.bin

CROSS := $(or $(CROSS),i686-linux-gnu-)
CC := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy

CFLAGS = -O2 -g -I ../../include

# Flags for dependency generation
override CPPFLAGS += -MMD -MP -MT $@ -MF $(@D)/$(*F).d
override CFLAGS += -march=i486 -Wall $(EXTRA_CFLAGS) -m16
override CFLAGS += -ffreestanding -I$(TOPSRC_DIR)/include

cc-test = $(CC) -Werror $1 -c -o /dev/null -xc /dev/null >/dev/null 2>/dev/null
cc-option = if $(call cc-test, $1); then \
    echo "$1 detected" && echo "override CFLAGS += $1" >&3; else \
    echo "$1 not detected" $(if $2,&& echo "override CFLAGS += $2" >&3); fi

# If -fcf-protection is enabled in flags or compiler defaults that will
# conflict with -march=i486
config-cc.mak: Makefile
	$(quiet-@)($(call cc-option,-fcf-protection=none); \
	    $(call cc-option,-fno-pie); \
	    $(call cc-option,-no-pie); \
	    $(call cc-option,-fno-stack-protector); \
	    $(call cc-option,-Wno-array-bounds)) 3> config-cc.mak
-include config-cc.mak

override LDFLAGS = -nostdlib -Wl,--build-id=none,-T,flat.lds,-m,elf_i386

pvh.img: pvh.o pvh_main.o

%.o: %.S
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

%.img: %.o
	$(CC) $(CFLAGS) $(LDFLAGS) -s -o $@ $^

%.raw: %.img
	$(OBJCOPY) -O binary -j .text $< $@

%.bin: %.raw
	$(PYTHON) ../../scripts/signrom.py $< $@

include $(wildcard *.d)

clean:
	rm -f *.o *.d *.raw *.img *.bin *~

distclean: clean
	rm -f config-cc.mak

.PHONY: all clean distclean
