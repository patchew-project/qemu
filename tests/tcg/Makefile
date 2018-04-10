# -*- Mode: makefile -*-
#
# TCG tests
#
# These are complicated by the fact we want to build them for guest
# systems. This requires knowing what guests we are building and which
# ones we have cross-compilers for or docker images with
# cross-compilers.
#
# The tests themselves should be as minimal as possible as
# cross-compilers don't always have a large amount of libraries
# available.
#
# We only include the host build system for SRC_PATH and we don't
# bother with the common rules.mk. We expect CC to have been set for
# us from the parent make. We also expect to be in the tests build dir
# for the FOO-linux-user.
#

-include ../../config-host.mak
-include ../config-target.mak

# Set search path for all sources
VPATH = $(SRC_PATH)/tests/tcg/multiarch
TEST_SRCS = $(wildcard $(SRC_PATH)/tests/tcg/multiarch/*.c)

VPATH     += $(SRC_PATH)/tests/tcg/$(ARCH)
TEST_SRCS += $(wildcard $(SRC_PATH)/tests/tcg/$(ARCH)/*.c)

SRCS=$(notdir ${TEST_SRCS})
TESTS=$(SRCS:.c=)

# We use what ever CC we have
CFLAGS=-Wall -O0 -g -fno-strict-aliasing -static
QEMU_CFLAGS=
LDFLAGS=

# The per ARCH target makefile which might add specific compiler flags
# for some compilation targets.

EXTRA_MAKEFILE=$(SRC_PATH)/tests/tcg/$(ARCH)/Makefile.target
CHECK_INCLUDE=$(wildcard $(EXTRA_MAKEFILE))

ifeq ($(EXTRA_MAKEFILE),$(CHECK_INCLUDE))
include $(EXTRA_MAKEFILE)
endif

testthread: LDFLAGS=-lpthread

%: %.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

all: $(TESTS)

# There is no clean target, the calling make just rm's the tests build dir
