# *****************************************************************************
# * Copyright (c) 2004, 2008 IBM Corporation
# * All rights reserved.
# * This program and the accompanying materials
# * are made available under the terms of the BSD License
# * which accompanies this distribution, and is available at
# * http://www.opensource.org/licenses/bsd-license.php
# *
# * Contributors:
# *     IBM Corporation - initial implementation
# ****************************************************************************/

include ../../../config-host.mak
include $(SRC_PATH)/rules.mak

LIBCCMNDIR = $(SRC_PATH)/pc-bios/s390-ccw/libc
STRINGCMNDIR = $(LIBCCMNDIR)/string
CTYPECMNDIR = $(LIBCCMNDIR)/ctype
STDLIBCMNDIR = $(LIBCCMNDIR)/stdlib
STDIOCMNDIR = $(LIBCCMNDIR)/stdio

QEMU_CFLAGS := $(filter -W%, $(QEMU_CFLAGS))
QEMU_CFLAGS += -ffreestanding -fno-delete-null-pointer-checks -msoft-float
QEMU_CFLAGS += -march=z900 -fPIE -fno-strict-aliasing
QEMU_CFLAGS += $(call cc-option, $(QEMU_CFLAGS), -fno-stack-protector)
QEMU_CFLAGS += -I$(LIBCCMNDIR)/include
LDFLAGS += -Wl,-pie -nostdlib

TARGET = ../libc.a

all: $(TARGET)

include $(STRINGCMNDIR)/Makefile.inc
include $(CTYPECMNDIR)/Makefile.inc
include $(STDLIBCMNDIR)/Makefile.inc
include $(STDIOCMNDIR)/Makefile.inc

OBJS = $(STRING_OBJS) $(CTYPE_OBJS) $(STDLIB_OBJS) $(STDIO_OBJS)


$(TARGET): $(OBJS)
	$(call quiet-command,$(AR) -rc $@ $(OBJS),"AR","$(TARGET_DIR)$@")

clean:
	rm -f $(TARGET) $(OBJS)

distclean: clean
