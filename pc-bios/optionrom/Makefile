all: build-all
# Dummy command so that make thinks it has done something
	@true

include ../../config-host.mak
include $(SRC_PATH)/rules.mak

$(call set-vpath, $(SRC_PATH)/pc-bios/optionrom)

.PHONY : all clean build-all

CFLAGS := -Wall -Wstrict-prototypes -Werror -fomit-frame-pointer -fno-builtin
CFLAGS += $(call cc-option, $(CFLAGS), -fno-stack-protector)
CFLAGS += $(CFLAGS_NOPIE)
CFLAGS += $(call cc-option, $(CFLAGS), -m16)
ifeq ($(filter -m16, $(CFLAGS)),)
# Attempt to work around compilers that lack -m16 (GCC <= 4.8, clang <= ??)
# On GCC we add -fno-toplevel-reorder to keep the order of asm blocks with
# respect to the rest of the code.  clang does not have -fno-toplevel-reorder,
# but it places all asm blocks at the beginning and we're relying on it for
# the option ROM header.  So just force clang not to use the integrated
# assembler, which doesn't support .code16gcc.
CFLAGS += -m32 -include $(SRC_PATH)/pc-bios/optionrom/code16gcc.h
CFLAGS += $(call cc-option, $(CFLAGS), -fno-toplevel-reorder)
CFLAGS += $(call cc-option, $(CFLAGS), -no-integrated-as)
endif

QEMU_INCLUDES += -I$(SRC_PATH)
QEMU_CFLAGS = $(CFLAGS)

ASFLAGS += -32

build-all: multiboot.bin linuxboot.bin linuxboot_dma.bin kvmvapic.bin

# suppress auto-removal of intermediate files
.SECONDARY:


%.o: %.S
	$(call quiet-command,$(CPP) $(QEMU_INCLUDES) $(QEMU_DGFLAGS) -c -o - $< | $(AS) $(ASFLAGS) -o $@,"  AS    $(TARGET_DIR)$@")

ifdef CONFIG_WIN32
LD_EMULATION = i386pe
else
LD_EMULATION = elf_i386
endif

%.img: %.o
	$(call quiet-command,$(LD) $(LDFLAGS_NOPIE) -m $(LD_EMULATION) -Ttext 0 -e _start -s -o $@ $<,"  Building $(TARGET_DIR)$@")

%.raw: %.img
	$(call quiet-command,$(OBJCOPY) -O binary -j .text $< $@,"  Building $(TARGET_DIR)$@")

%.bin: %.raw
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/signrom.py $< $@,"  Signing $(TARGET_DIR)$@")

clean:
	rm -f *.o *.d *.raw *.img *.bin *~
