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

$(call set-vpath, $(SRC_PATH)/pc-bios/s390-ccw/libnet)

QEMU_CFLAGS := $(filter -W%, $(QEMU_CFLAGS))
QEMU_CFLAGS += -ffreestanding -fno-delete-null-pointer-checks -msoft-float
QEMU_CFLAGS += -march=z900 -fPIE -fno-strict-aliasing -Wno-redundant-decls
QEMU_CFLAGS += -I$(SRC_PATH)/pc-bios/s390-ccw/libnet
QEMU_CFLAGS += -I$(SRC_PATH)/pc-bios/s390-ccw/libc/include
QEMU_CFLAGS += $(call cc-option, $(QEMU_CFLAGS), -fno-stack-protector)
LDFLAGS += -Wl,-pie -nostdlib

SRCS =	ethernet.c ipv4.c udp.c tcp.c dns.c dhcp.c tftp.c \
	ipv6.c dhcpv6.c icmpv6.c ndp.c netload.c args.c timer.c

OBJS = $(SRCS:%.c=%.o)

TARGET = ../libnet.a

all: $(TARGET)

$(TARGET): $(OBJS)
	$(call quiet-command,$(AR) -rc $@ $(OBJS),"AR","$(TARGET_DIR)$@")

clean:
	rm -f $(TARGET) $(OBJS)

distclean: clean

