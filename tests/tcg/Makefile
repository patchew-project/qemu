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
# bother with the common rules.mk. We expect CC/LD to have been set for
# us from the parent make as well as being passed EXTRA_CFLAGS.
#
# We also expect to be in the tests build dir for the FOO-linux-user.
#

-include ../../config-host.mak
-include ../config-target.mak

# Tests we are building
TESTS=

# Start with a blank slate, the build targets get to add stuff first
CFLAGS=
QEMU_CFLAGS=
LDFLAGS=

# The order we include is important. We include multiarch, base arch and finally arch
-include $(SRC_PATH)/tests/tcg/multiarch/Makefile.target
-include $(SRC_PATH)/tests/tcg/$(TARGET_BASE_ARCH)/Makefile.target
ifneq ($(TARGET_BASE_ARCH),$(TARGET_NAME))
-include $(SRC_PATH)/tests/tcg/$(TARGET_NAME)/Makefile.target
endif

# Add the common build options
CFLAGS+=-Wall -O0 -g -fno-strict-aliasing
LDFLAGS+=-static

%: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $< -o $@ $(LDFLAGS)

all: $(TESTS)

# There is no clean target, the calling make just rm's the tests build dir
