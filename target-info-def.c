/*
 * QEMU target info stubs (target specific)
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define IN_TARGET_INFO_DEF 1

#include "qemu/osdep.h"
#include "qemu/target-info.h"
#include "qemu/target-info-def.h"
#include "qemu/target-info-init.h"
#include "hw/core/boards.h"
#include "cpu.h"
#include "exec/cpu-defs.h"
#include "exec/page-vary.h"

/* Validate correct placement of CPUArchState. */
QEMU_BUILD_BUG_ON(offsetof(ArchCPU, parent_obj) != 0);
QEMU_BUILD_BUG_ON(offsetof(ArchCPU, env) != sizeof(CPUState));

/* Validate target page size, if invariant. */
#ifndef TARGET_PAGE_BITS_VARY
QEMU_BUILD_BUG_ON(TARGET_PAGE_BITS < TARGET_PAGE_BITS_MIN);
#endif

#ifdef TARGET_RUNTIME_ENDIAN
/* This writable definition must not be compiled with -flto. */
TargetInfo target_info_stub = {
#else
static const TargetInfo target_info_stub = {
#endif
    .target_name = TARGET_NAME,
    .target_arch = glue(SYS_EMU_TARGET_, TARGET_ARCH_VARIANT),
    .long_bits = TARGET_LONG_BITS,
    .cpu_type = CPU_RESOLVING_TYPE,
    .endianness = TARGET_BIG_ENDIAN ? ENDIAN_MODE_BIG : ENDIAN_MODE_LITTLE,
#ifdef TARGET_PAGE_BITS_VARY
    .page_bits_vary = true,
# ifdef TARGET_PAGE_BITS_LEGACY
    .page_bits_init = TARGET_PAGE_BITS_LEGACY,
# endif
#else
    .page_bits_vary = false,
    .page_bits_init = TARGET_PAGE_BITS,
#endif
};

target_info_init(target_info_stub)

#ifdef TARGET_RUNTIME_ENDIAN
void target_set_big_endian(bool big_endian)
{
    static bool endian_decided;

    assert(!endian_decided);
    endian_decided = true;
    target_info_stub.endianness = big_endian ? ENDIAN_MODE_BIG
                                             : ENDIAN_MODE_LITTLE;
}
#endif
