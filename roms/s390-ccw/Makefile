all: s390-ccw.img

CROSS := $(or $(CROSS),s390x-linux-gnu-)
AR := $(CROSS)ar
CC := $(CROSS)gcc
STRIP := $(CROSS)strip

CFLAGS = -O2 -g -I ../../include/hw/s390x/ipl
LDFLAGS ?=
MAKEFLAGS += -rR

GIT_SUBMODULES = roms/SLOF

# Flags for dependency generation
QEMU_DGFLAGS = -MMD -MP -MT $@ -MF $(@D)/$(*F).d

%.o: %.c
	$(CC) $(EXTRA_CFLAGS) $(QEMU_DGFLAGS) $(CFLAGS) -c -o $@ $<

%.o: %.S
	$(CC) $(EXTRA_CFLAGS) $(QEMU_DGFLAGS) $(CFLAGS) -c -o $@ $<

.PHONY : all clean distclean

OBJECTS = start.o main.o bootmap.o jump2ipl.o sclp.o menu.o netmain.o \
	  virtio.o virtio-net.o virtio-scsi.o virtio-blkdev.o cio.o dasd-ipl.o \
	  virtio-ccw.o clp.o pci.o virtio-pci.o secure-ipl.o

SLOF_DIR := ../../roms/SLOF

LIBC_INC := -nostdinc -I$(SLOF_DIR)/lib/libc/include
LIBNET_INC := -I$(SLOF_DIR)/lib/libnet

EXTRA_CFLAGS += -Wall
EXTRA_CFLAGS += -ffreestanding -fno-delete-null-pointer-checks -fno-common -fPIE
EXTRA_CFLAGS += -fwrapv -fno-strict-aliasing -fno-asynchronous-unwind-tables
EXTRA_CFLAGS += -msoft-float
EXTRA_CFLAGS += -std=gnu99
EXTRA_CFLAGS += $(LIBC_INC) $(LIBNET_INC)
EXTRA_LDFLAGS += -static-pie -nostdlib -z noexecstack -z text

cc-test = $(CC) -Werror $1 -c -o /dev/null -xc /dev/null >/dev/null 2>/dev/null
cc-option = if $(call cc-test, $1); then \
    echo "$1 detected" && echo "EXTRA_CFLAGS += $1" >&3; else \
    echo "$1 not detected" $(if $2,&& echo "EXTRA_CFLAGS += $2" >&3); fi

config-cc.mak: Makefile
	$(quiet-@)($(call cc-option,-Wno-stringop-overflow); \
	    $(call cc-option,-fno-stack-protector); \
	    $(call cc-option,-Wno-array-bounds); \
	    $(call cc-option,-Wno-gnu); \
	    $(call cc-option,-march=z900,-march=z10)) 3> config-cc.mak
-include config-cc.mak

# libc files:

LIBC_CFLAGS = $(EXTRA_CFLAGS) $(CFLAGS) $(LIBC_INC) $(LIBNET_INC) \
	      -MMD -MP -MT $@ -MF $(@:%.o=%.d)

CTYPE_OBJS = isdigit.o isxdigit.o toupper.o
%.o : $(SLOF_DIR)/lib/libc/ctype/%.c
	$(CC) $(LIBC_CFLAGS) -c -o $@ $<

STRING_OBJS = strcat.o strchr.o strrchr.o strcpy.o strlen.o strncpy.o \
	      strcmp.o strncmp.o strcasecmp.o strncasecmp.o strstr.o \
	      memset.o memcpy.o memmove.o memcmp.o
%.o : $(SLOF_DIR)/lib/libc/string/%.c
	$(CC) $(LIBC_CFLAGS) -c -o $@ $<

STDLIB_OBJS = atoi.o atol.o strtoul.o strtol.o rand.o malloc.o free.o
%.o : $(SLOF_DIR)/lib/libc/stdlib/%.c
	$(CC) $(LIBC_CFLAGS) -c -o $@ $<

STDIO_OBJS = sprintf.o snprintf.o vfprintf.o vsnprintf.o vsprintf.o fprintf.o \
	     printf.o putc.o puts.o putchar.o stdchnls.o fileno.o
%.o : $(SLOF_DIR)/lib/libc/stdio/%.c
	$(CC) $(LIBC_CFLAGS) -c -o $@ $<

sbrk.o: $(SLOF_DIR)/slof/sbrk.c
	$(CC) $(LIBC_CFLAGS) -c -o $@ $<

LIBCOBJS := $(STRING_OBJS) $(CTYPE_OBJS) $(STDLIB_OBJS) $(STDIO_OBJS) sbrk.o

libc.a: $(LIBCOBJS)
	$(AR) -rc $@ $^

# libnet files:

LIBNETOBJS := args.o dhcp.o dns.o icmpv6.o ipv6.o tcp.o udp.o bootp.o \
	      dhcpv6.o ethernet.o ipv4.o ndp.o tftp.o pxelinux.o
LIBNETCFLAGS = $(EXTRA_CFLAGS) $(CFLAGS) $(LIBC_INC) $(LIBNET_INC) \
	       -DDHCPARCH=0x1F -MMD -MP -MT $@ -MF $(@:%.o=%.d)

%.o : $(SLOF_DIR)/lib/libnet/%.c
	$(CC) $(LIBNETCFLAGS) -c -o $@ $<

libnet.a: $(LIBNETOBJS)
	$(AR) -rc $@ $^

s390-ccw.elf: $(OBJECTS) libnet.a libc.a
	$(CC) $(EXTRA_LDFLAGS) $(LDFLAGS) -o $@ $^

s390-ccw.img: s390-ccw.elf
	$(STRIP) --strip-unneeded $< -o $@

$(OBJECTS): Makefile

ALL_OBJS = $(sort $(OBJECTS) $(LIBCOBJS) $(LIBNETOBJS))
-include $(ALL_OBJS:%.o=%.d)

clean:
	rm -f *.o *.d *.img *.elf *~ *.a

distclean: clean
	rm -f config-cc.mak

.PHONY: git-submodule-update
$(SRC_PATH)/../../.git-submodule-status: git-submodule-update
Makefile: $(SRC_PATH)/../../.git-submodule-status

git-submodule-update:
ifneq ($(GIT_SUBMODULES_ACTION),ignore)
	$(quiet-@)GIT=git "../../scripts/git-submodule.sh" $(GIT_SUBMODULES_ACTION) $(GIT_SUBMODULES)
endif
