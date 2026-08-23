/*
 * QEMU TargetInfo structure definition
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_TARGET_INFO_IMPL_H
#define QEMU_TARGET_INFO_IMPL_H

#include "qapi/qapi-types-common.h"
#include "qapi/qapi-types-machine.h"

typedef struct TargetInfo {
    /* runtime equivalent of TARGET_NAME definition */
    const char *target_name;
    /* related to TARGET_ARCH definition */
    SysEmuTarget target_arch;
    /* runtime equivalent of TARGET_LONG_BITS definition */
    unsigned long_bits;
    /* runtime equivalent of CPU_RESOLVING_TYPE definition */
    const char *cpu_type;
    /* related to TARGET_BIG_ENDIAN definition */
    EndianMode endianness;
    /*
     * True on the endian variant that matches this translation unit's
     * TARGET_BIG_ENDIAN. target_info_qom_set_target() uses this when
     * more than one TargetInfo is registered and -target is omitted.
     */
    bool is_default;
    /*
     * runtime equivalent of
     *   TARGET_PAGE_BITS_VARY ? TARGET_PAGE_BITS_LEGACY : TARGET_PAGE_BITS
     */
    unsigned page_bits_init;
    /* runtime equivalent of TARGET_PAGE_BITS_VARY definition */
    bool page_bits_vary;

    /* CONFIG_MULTIPROCESS */
    bool config_multiprocess;
    /* CONFIG_NITRO */
    bool config_nitro;
    /* CONFIG_XEN */
    bool config_xen;
} TargetInfo;

/**
 * target_info:
 *
 * Returns: The TargetInfo structure definition for this target binary.
 */
const TargetInfo *target_info(void);

#endif
