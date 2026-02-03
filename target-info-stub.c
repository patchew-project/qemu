/*
 * QEMU target info stubs (target specific)
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/target-info.h"
#include "qemu/target-info-impl.h"
#include "hw/core/boards.h"
#include "qapi/error.h"
#include "cpu.h"

/* Validate correct placement of CPUArchState. */
QEMU_BUILD_BUG_ON(offsetof(ArchCPU, parent_obj) != 0);
QEMU_BUILD_BUG_ON(offsetof(ArchCPU, env) != sizeof(CPUState));

static TargetInfo target_info_stub = {
    .target_name = TARGET_NAME,
    .target_arch = SYS_EMU_TARGET__MAX,
    .long_bits = TARGET_LONG_BITS,
    .cpu_type = CPU_RESOLVING_TYPE,
    .machine_typename = TYPE_MACHINE,
    .endianness = TARGET_BIG_ENDIAN ? ENDIAN_MODE_BIG : ENDIAN_MODE_LITTLE,
};

const TargetInfo *target_info(void)
{
    return &target_info_stub;
}

__attribute__((constructor))
static void init_target_arch(void)
{
    target_info_stub.target_arch = qapi_enum_parse(&SysEmuTarget_lookup,
                                                   target_name(), -1,
                                                   &error_abort);
}
