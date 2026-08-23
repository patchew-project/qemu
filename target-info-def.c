/*
 * QEMU target info definition (target specific)
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/target-info.h"
#include "qemu/target-info-impl.h"
#include "qemu/target-info-init.h"
#include "hw/core/boards.h"
#include "cpu.h"
#include "exec/cpu-defs.h"
#include "exec/page-vary.h"
#ifndef CONFIG_USER_ONLY
#include CONFIG_DEVICES
#endif
#include CONFIG_TARGET

/* Validate correct placement of CPUArchState. */
QEMU_BUILD_BUG_ON(offsetof(ArchCPU, parent_obj) != 0);
QEMU_BUILD_BUG_ON(offsetof(ArchCPU, env) != sizeof(CPUState));

/* Validate target page size, if invariant. */
#ifndef TARGET_PAGE_BITS_VARY
QEMU_BUILD_BUG_ON(TARGET_PAGE_BITS < TARGET_PAGE_BITS_MIN);
#endif

#ifdef TARGET_PAGE_BITS_VARY
# ifdef TARGET_PAGE_BITS_LEGACY
#  define TARGET_INFO_PAGE_BITS \
    .page_bits_vary = true, \
    .page_bits_init = TARGET_PAGE_BITS_LEGACY,
# else
#  define TARGET_INFO_PAGE_BITS \
    .page_bits_vary = true,
# endif
#else
# define TARGET_INFO_PAGE_BITS \
    .page_bits_vary = false, \
    .page_bits_init = TARGET_PAGE_BITS,
#endif

#ifndef CONFIG_USER_ONLY
# ifdef CONFIG_MULTIPROCESS
#  define TARGET_INFO_CONFIG_MULTIPROCESS .config_multiprocess = true,
# else
#  define TARGET_INFO_CONFIG_MULTIPROCESS .config_multiprocess = false,
# endif
# ifdef CONFIG_NITRO
#  define TARGET_INFO_CONFIG_NITRO .config_nitro = true,
# else
#  define TARGET_INFO_CONFIG_NITRO .config_nitro = false,
# endif
# ifdef CONFIG_XEN
#  define TARGET_INFO_CONFIG_XEN .config_xen = true,
# else
#  define TARGET_INFO_CONFIG_XEN .config_xen = false,
# endif
# define TARGET_INFO_CONFIG \
    TARGET_INFO_CONFIG_MULTIPROCESS \
    TARGET_INFO_CONFIG_NITRO \
    TARGET_INFO_CONFIG_XEN
#else
# define TARGET_INFO_CONFIG
#endif

#define TARGET_INFO_COMMON                                                  \
    .target_name = TARGET_NAME,                                             \
    .target_arch = glue(SYS_EMU_TARGET_, TARGET_ARCH),                      \
    .long_bits = TARGET_LONG_BITS,                                          \
    .cpu_type = CPU_RESOLVING_TYPE,                                         \
    TARGET_INFO_PAGE_BITS                                                   \
    TARGET_INFO_CONFIG

#ifdef CONFIG_USER_ONLY
static const TargetInfo target_info_def = {
    TARGET_INFO_COMMON
    .endianness = TARGET_BIG_ENDIAN ? ENDIAN_MODE_BIG : ENDIAN_MODE_LITTLE,
};

target_info_init(target_info_def)
#else
static const TargetInfo target_info_le = {
    TARGET_INFO_COMMON
    .endianness = ENDIAN_MODE_LITTLE,
    .is_default = !TARGET_BIG_ENDIAN,
};

static const TargetInfo target_info_be = {
    TARGET_INFO_COMMON
    .endianness = ENDIAN_MODE_BIG,
    .is_default = TARGET_BIG_ENDIAN,
};

target_info_init_le_be(target_info_le, target_info_be)
#endif
