# -*- Mode: makefile -*-
#
# MIPS MSA specific TCG tests
#
# Copyright (c) 2021 Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

MSA_DIR = $(SRC_PATH)/tests/tcg/mips/user/ase/msa

MSA_TEST_CLASS = bit-count bit-move bit-set fixed-multiply \
				float-max-min int-add int-average int-compare int-divide \
				int-dot-product interleave int-max-min int-modulo \
				int-multiply int-subtract logic move pack shift

MSA_TEST_SRCS = $(foreach class,$(MSA_TEST_CLASS),$(wildcard $(MSA_DIR)/$(class)/*.c))

MSA_TESTS = $(patsubst %.c,%,$(notdir $(MSA_TEST_SRCS)))

$(MSA_TESTS): CFLAGS+=-mmsa $(MSA_CFLAGS)
$(MSA_TESTS): %: $(foreach CLASS,$(MSA_TEST_CLASS),$(wildcard $(MSA_DIR)/$(CLASS)/%.c))
	$(CC) -static $(CFLAGS) -o $@ \
		$(foreach CLASS,$(MSA_TEST_CLASS),$(wildcard $(MSA_DIR)/$(CLASS)/$@.c))

$(foreach test,$(MSA_TESTS),run-$(test)): QEMU_OPTS += -cpu $(MSA_CPU)

# FIXME: These tests fail when using plugins
ifneq ($(CONFIG_PLUGIN),y)
TESTS += $(MSA_TESTS)
endif
