CURRENT_MAKEFILE := $(realpath $(word $(words $(MAKEFILE_LIST)),$(MAKEFILE_LIST)))
SRC_DIR := $(dir $(CURRENT_MAKEFILE))
TOPSRC_DIR := $(SRC_DIR)/../..
VPATH = $(SRC_DIR)

all:
# Dummy command so that make thinks it has done something
	@true

include ../../config-host.mak

quiet-command = $(if $(V),$1,$(if $(2),@printf "  %-7s %s\n" $2 $3 && $1, @$1))
cc-option = $(if $(shell $(CC) $1 -S -o /dev/null -xc /dev/null >/dev/null 2>&1 && echo OK), $1, $2)

override CFLAGS += $(filter -W%, $(QEMU_CFLAGS))
override CFLAGS += -ffreestanding -fno-delete-null-pointer-checks -msoft-float
override CFLAGS += -march=z900 -fPIE -fno-strict-aliasing
override CFLAGS += -fno-asynchronous-unwind-tables
override CFLAGS += $(call cc-option, -fno-stack-protector)
LDFLAGS += -Wl,-pie -nostdlib

OBJECTS = start.o main.o bootmap.o jump2ipl.o sclp.o menu.o \
	  virtio.o virtio-scsi.o virtio-blkdev.o libc.o cio.o dasd-ipl.o

STRIP ?= strip

$(OBJECTS): Makefile

SLOF_DIR := $(SRC_PATH)/roms/SLOF

NETOBJS := start.o sclp.o cio.o virtio.o virtio-net.o jump2ipl.o netmain.o \

LIBC_INC := -nostdinc -I$(SLOF_DIR)/lib/libc/include
LIBNET_INC := -I$(SLOF_DIR)/lib/libnet

$(NETOBJS): QEMU_CFLAGS += $(LIBC_INC) $(LIBNET_INC)

# libc files:

LIBC_CFLAGS :=  $(QEMU_CFLAGS) $(CFLAGS) $(LIBC_INC) $(LIBNET_INC) \
	-MMD -MP -MT $@ -MF $(@:%.o=%.d)


CTYPE_OBJS = isdigit.o isxdigit.o toupper.o
$(CTYPE_OBJS) : %.o : $(SLOF_DIR)/lib/libc/ctype/%.c
	$(call quiet-command,$(CC) $(LIBC_CFLAGS) -c -o $@ $<,"CC","$(TARGET_DIR)$@")

STRING_OBJS = strcat.o strchr.o strrchr.o strcpy.o strlen.o strncpy.o \
	      strcmp.o strncmp.o strcasecmp.o strncasecmp.o strstr.o \
	      memset.o memcpy.o memmove.o memcmp.o
$(STRING_OBJS) : %.o : $(SLOF_DIR)/lib/libc/string/%.c
	$(call quiet-command,$(CC) $(LIBC_CFLAGS) -c -o $@ $<,"CC","$(TARGET_DIR)$@")

STDLIB_OBJS = atoi.o atol.o strtoul.o strtol.o rand.o malloc.o free.o
$(STDLIB_OBJS) : %.o : $(SLOF_DIR)/lib/libc/stdlib/%.c
	$(call quiet-command,$(CC) $(LIBC_CFLAGS) -c -o $@ $<,"CC","$(TARGET_DIR)$@")

STDIO_OBJS = sprintf.o snprintf.o vfprintf.o vsnprintf.o vsprintf.o fprintf.o \
	     printf.o putc.o puts.o putchar.o stdchnls.o fileno.o
$(STDIO_OBJS) : %.o : $(SLOF_DIR)/lib/libc/stdio/%.c
	$(call quiet-command,$(CC) $(LIBC_CFLAGS) -c -o $@ $<,"CC","$(TARGET_DIR)$@")

sbrk.o: $(SLOF_DIR)/slof/sbrk.c
	$(call quiet-command,$(CC) $(LIBC_CFLAGS) -c -o $@ $<,"CC","$(TARGET_DIR)$@")

LIBCOBJS := $(STRING_OBJS) $(CTYPE_OBJS) $(STDLIB_OBJS) $(STDIO_OBJS) sbrk.o

libc.a: $(LIBCOBJS)
	$(call quiet-command,$(AR) -rc $@ $^,"AR","$(TARGET_DIR)$@")

# libnet files:

LIBNETOBJS := args.o dhcp.o dns.o icmpv6.o ipv6.o tcp.o udp.o bootp.o \
	      dhcpv6.o ethernet.o ipv4.o ndp.o tftp.o pxelinux.o
LIBNETCFLAGS := $(CFLAGS) -DDHCPARCH=0x1F $(LIBC_INC) $(LIBNET_INC) \
	-DDHCPARCH=0x1F -MMD -MP -MT $@ -MF $(@:%.o=%.d)

$(LIBNETOBJS) : %.o : $(SLOF_DIR)/lib/libnet/%.c
	$(call quiet-command,$(CC) $(LIBNETCFLAGS) -c -o $@ $<,"CC","$(TARGET_DIR)$@")

libnet.a: $(LIBNETOBJS)
	$(call quiet-command,$(AR) -rc $@ $^,"AR","$(TARGET_DIR)$@")

%.elf:
	$(call quiet-command,$(CC) $(LDFLAGS) -o $@ $^,"BUILD","$(TARGET_DIR)$@")

%.img: %.elf
	$(call quiet-command,$(STRIP) --strip-unneeded $< -o $@,"STRIP","$(TARGET_DIR)$@")

s390-ccw.elf: $(OBJECTS)
s390-ccw.img: s390-ccw.elf
all: s390-ccw.img

ifneq ($(wildcard $(SLOF_DIR)/lib/libnet),)
s390-netboot.elf: $(NETOBJS) libnet.a libc.a
s390-netboot.elf: LDFLAGS += -Ttext=0x7800000

s390-netboot.img: s390-netboot.elf
all: s390-netboot.img
endif

ALL_OBJS = $(sort $(OBJECTS) $(NETOBJS) $(LIBCOBJS) $(LIBNETOBJS))
-include $(ALL_OBJS:%.o=%.d)

clean:
	rm -f *.o *.d *.img *.elf *~ *.a

.PHONY : all clean
