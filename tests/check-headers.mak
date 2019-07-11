# -*- Mode: makefile -*-

ifneq ($(wildcard $(SRC_PATH)/.git),)

# All headers:
src-headers := $(filter %.h, $(shell cd $(SRC_PATH) && git ls-files))

# Headers we don't want to test
# Third party headers we don't want to mess with
excluded-headers := $(filter disas/libvixl/vixl/% include/hw/xen/interface/% include/standard-headers/% linux-headers/% pc-bios/% slirp/%, $(src-headers))
# Funny stuff we don't want to mess with
excluded-headers += $(filter tests/multiboot/% tests/tcg/% tests/uefi-test-tools/%, $(src-headers))
excluded-headers += scripts/cocci-macro-file.h
# Exclude all but include/ for now:
excluded-headers += $(filter-out include/%, $(src-headers))

# Headers for target-dependent code only (require -DNEED_CPU_H etc.)
target-header-regexp := NOTE: May only be included into target-dependent code
target-headers := $(shell cd $(SRC_PATH) && egrep -l '$(target-header-regexp)' $(src-headers))

# Headers for target-independent code only
untarget-headers := include/exec/poison.h

# Headers not for user emulation (include hw/hw.h)
hw-header-regexp := NOTE: May not be included into user emulation code
hw-headers := $(shell cd $(SRC_PATH) && egrep -l '$(hw-header-regexp)' $(src-headers))

# Headers carrying a FIXME about this test
# Extended regular expression matching the FIXME comment in headers
# not expected to pass the test in this build's configuration:
bad-header-regexp := FIXME Does not pass make check-headers(
# Fails in %-user:
ifneq ($(TARGET_DIR),)
ifneq ($(CONFIG_USER_ONLY),)
bad-header-regexp += for user emulation|
endif
endif
# Target-dependent arm only:
ifneq ($(TARGET_BASE_ARCH),arm)
bad-header-regexp += for TARGET_BASE_ARCH other than arm|
endif
# Target-dependent i386 only:
ifneq ($(TARGET_BASE_ARCH),i386)
bad-header-regexp += for TARGET_BASE_ARCH other than i386|
endif
# Target-dependent mips only:
ifneq ($(TARGET_BASE_ARCH),mips)
bad-header-regexp += for TARGET_BASE_ARCH other than mips|
endif
# Require <cpuid.h>:
ifneq ($(CONFIG_CPUID_H),y)
bad-header-regexp += without CONFIG_CPUID_H|
endif
# Require Linux:
ifneq ($(CONFIG_LINUX),y)
bad-header-regexp += without CONFIG_LINUX|
endif
# Require OpenGL:
ifneq ($(CONFIG_OPENGL),y)
bad-header-regexp += without CONFIG_OPENGL|
endif
# Require Pixman:
# since there's no easy, precise way to detect "have pixman",
# approximate with CONFIG_SOFTMMU
ifneq ($(CONFIG_SOFTMMU),y)
bad-header-regexp += without pixman|
endif
# Require POSIX:
ifneq ($(CONFIG_POSIX),y)
bad-header-regexp += without CONFIG_POSIX|
endif
# Require SPICE:
ifneq ($(CONFIG_SPICE),y)
bad-header-regexp += without CONFIG_SPICE|
endif
# Require any system emulator being built
# can't use CONFIG_SOFTMMU, it's off in TARGET_DIR=%-user; check
# TARGET_DIRS instead
ifeq ($(filter %-softmmu, $(TARGET_DIRS)),)
bad-header-regexp += without system emulation|
endif
# Require Windows:
ifneq ($(CONFIG_WIN32),y)
bad-header-regexp += without CONFIG_WIN32|
endif
# Require Xen:
ifneq ($(CONFIG_XEN),y)
bad-header-regexp += without CONFIG_XEN|
endif
bad-header-regexp += D06F00D to avoid empty RE)?, yet!
# The headers not expected to pass the test in this build's configuration:
bad-headers := $(shell cd $(SRC_PATH) && egrep -l '$(bad-header-regexp)' $(src-headers))

# Checked headers (all less excluded and bad):
# to be checked target-independently: all less excluded, bad, and target
checked-headers := $(filter-out $(excluded-headers) $(bad-headers) $(target-headers), $(src-headers))
check-header-tests := $(patsubst %.h, tests/headers/%.c, $(checked-headers))
# to be checked for each target: all less excluded, bad, and untarget
checked-target-headers := $(filter-out $(excluded-headers) $(bad-headers) $(untarget-headers), $(src-headers))
# less hw for user emulation targets
ifneq ($(TARGET_DIR),)
ifneq ($(CONFIG_USER_ONLY),)
checked-target-headers := $(filter-out $(hw-headers), $(checked-target-headers))
endif
endif
check-target-header-tests := $(patsubst %.h, tests/headers-tgt/%.c, $(checked-target-headers))

# Bad headers (all less excluded and checked):
# to be checked target-independently: bad less excluded and target
unchecked-headers := $(filter-out $(excluded-headers) $(target-headers), $(bad-headers))
check-bad-header-tests := $(patsubst %.h, tests/headers/%.c, $(unchecked-headers))
# to be checked for each target: bad less excluded and untarget
unchecked-target-headers := $(filter-out $(excluded-headers) $(untarget-headers), $(bad-headers))
check-bad-target-header-tests := $(patsubst %.h, tests/headers-tgt/%.c, $(unchecked-target-headers))

endif
