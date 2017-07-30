include ../../../config-host.mak
include ../../config-target.mak
include $(SRC_PATH)/rules.mak

vpath % $(SRC_PATH)/hypertrace/guest

# do not use QEMU's per-host cflags when building guest code
QEMU_CFLAGS  = -Werror -Wall

QEMU_CFLAGS += $(GLIB_CFLAGS)
QEMU_CFLAGS += -I$(SRC_PATH)/include
QEMU_CFLAGS += -I../../../linux-headers
QEMU_CFLAGS += -I../../../
QEMU_CFLAGS += -I../../

ifdef CONFIG_SOFTMMU
QEMU_CFLAGS += -DNEED_CPU_H
QEMU_CFLAGS += -I$(SRC_PATH)/target-$(TARGET_BASE_ARCH)
endif

QEMU_CFLAGS += -g -O2

obj-y = common.o

libqemu-hypertrace-guest.a: $(obj-y)

all: libqemu-hypertrace-guest.a

clean:
	rm -f $(obj-y) libqemu-hypertrace-guest.a
