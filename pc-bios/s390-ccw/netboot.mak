
SLOF_DIR := $(SRC_PATH)/roms/SLOF

NETOBJS := start.o sclp.o virtio.o netmain.o sbrk.o libc.a

LIBC_INC := -I$(SLOF_DIR)/lib/libc/include
LIBNET_INC := -I$(SLOF_DIR)/lib/libnet

NETLDFLAGS := $(LDFLAGS) -Ttext=0x7800000

$(NETOBJS): CFLAGS += $(LIBC_INC) $(LIBNET_INC)

s390-netboot.elf: $(NETOBJS)
	$(call quiet-command,$(CC) $(NETLDFLAGS) -o $@ $(NETOBJS),"BUILD","$(TARGET_DIR)$@")

s390-netboot.img: s390-netboot.elf
	$(call quiet-command,$(STRIP) --strip-unneeded $< -o $@,"STRIP","$(TARGET_DIR)$@")


LIBCCMNDIR = $(SLOF_DIR)/lib/libc
STRINGCMNDIR = $(LIBCCMNDIR)/string
CTYPECMNDIR = $(LIBCCMNDIR)/ctype
STDLIBCMNDIR = $(LIBCCMNDIR)/stdlib
STDIOCMNDIR = $(LIBCCMNDIR)/stdio

include $(STRINGCMNDIR)/Makefile.inc
include $(CTYPECMNDIR)/Makefile.inc
include $(STDLIBCMNDIR)/Makefile.inc
include $(STDIOCMNDIR)/Makefile.inc

STDIO_OBJS = sprintf.o vfprintf.o vsnprintf.o vsprintf.o fprintf.o \
	     printf.o putc.o puts.o putchar.o stdchnls.o fileno.o

LIBCOBJS := $(STRING_OBJS) $(CTYPE_OBJS) $(STDLIB_OBJS) $(STDIO_OBJS)
$(LIBCOBJS): CFLAGS += $(LIBC_INC) $(QEMU_CFLAGS)

libc.a: $(LIBCOBJS)
	$(call quiet-command,$(AR) -rc $@ $^,"AR","$(TARGET_DIR)$@")

sbrk.o: $(SLOF_DIR)/slof/sbrk.c
	$(call quiet-command,$(CC) $(QEMU_CFLAGS) $(LIBC_INC) -c -o $@ $<,"CC","$(TARGET_DIR)$@")
