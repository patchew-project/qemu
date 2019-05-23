# -*- Mode: makefile -*-

ifneq ($(wildcard $(SRC_PATH)/.git),)

# All headers:
src-headers := $(filter %.h, $(shell cd $(SRC_PATH) && git ls-files))

# Third party headers we don't want to mess with:
excluded-headers := $(filter disas/libvixl/vixl/% include/standard-headers/% linux-headers/% pc-bios/% slirp/%, $(src-headers))
# Funny stuff we don't want to mess with:
excluded-headers += $(filter tests/multiboot/% tests/tcg/% tests/uefi-test-tools/%, $(src-headers))
excluded-headers += scripts/cocci-macro-file.h

# Headers that require -DNEED_CPU_H etc.
target-header-comment := NOTE: May only be included into target-dependent code
target-headers := $(shell cd $(SRC_PATH) && egrep -l '$(target-header-comment)' $(src-headers))

# Headers carrying a FIXME about this test:
bad-header-without-linux$(CONFIG_LINUX) := | without CONFIG_LINUX
bad-header-without-opengl$(CONFIG_OPENGL) := | without CONFIG_OPENGL
bad-header-without-posix$(CONFIG_POSIX) := | without CONFIG_POSIX
bad-header-without-rdma$(CONFIG_RDMA) := | without CONFIG_RDMA
bad-header-without-replication$(CONFIG_REPLICATION) := | without CONFIG_REPLICATION
bad-header-without-spice$(CONFIG_SPICE) := | without CONFIG_SPICE
bad-header-without-system-emu$(TARGET_DIRS) := | without system emulation
bad-header-without-win32$(CONFIG_WIN32) := | without CONFIG_WIN32
bad-header-without-x11$(CONFIG_X11) := | without CONFIG_X11
bad-header-without-xen$(CONFIG_XEN) := | without CONFIG_XEN
bad-header-comment := FIXME Does not pass make check-headers($(bad-header-without-linux)$(bad-header-without-opengl)$(bad-header-without-posix)$(bad-header-without-rdma)$(bad-header-without-replication)$(bad-header-without-spice)$(bad-header-without-system-emu)$(bad-header-without-win32)$(bad-header-without-x11)$(bad-header-without-xen)), yet!
bad-headers := $(shell cd $(SRC_PATH) && egrep -l '$(bad-header-comment)' $(src-headers))
bad-target-headers := $(filter $(target-headers), $(bad-headers))

# Checked headers (all less excluded and bad):
checked-headers := $(filter-out $(excluded-headers) $(bad-headers) $(target-headers), $(src-headers))
check-header-tests := $(patsubst %.h, tests/headers/%.c, $(checked-headers))
checked-target-headers := $(filter-out $(excluded-headers) $(bad-headers), $(target-headers))
check-target-header-tests := $(patsubst %.h, tests/headers/%.c, $(checked-target-headers))

# Bad headers (all less excluded and checked):
check-bad-header-tests := $(patsubst %.h, tests/headers/%.c, $(filter-out $(bad-target-headers), $(bad-headers)))
check-bad-target-header-tests := $(patsubst %.h, tests/headers/%.c, $(bad-target-headers))

endif
