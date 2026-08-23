/*
 * QEMU target info helpers
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/target-info.h"
#include "qemu/target-info-qapi.h"
#include "qemu/target-info-impl.h"
#include "qemu/target-info-qom.h"
#include "qapi/error.h"

const char *target_name(void)
{
    return target_info()->target_name;
}

unsigned target_long_bits(void)
{
    return target_info()->long_bits;
}

SysEmuTarget target_arch(void)
{
    return target_info()->target_arch;
}

const char *target_cpu_type(void)
{
    return target_info()->cpu_type;
}

EndianMode target_endian_mode(void)
{
    return target_info()->endianness;
}

bool target_big_endian(void)
{
    return target_endian_mode() == ENDIAN_MODE_BIG;
}

bool target_base_arm(void)
{
    switch (target_arch()) {
    case SYS_EMU_TARGET_ARM:
    case SYS_EMU_TARGET_AARCH64:
        return true;
    default:
        return false;
    }
}

bool target_arm(void)
{
    return target_arch() == SYS_EMU_TARGET_ARM;
}

bool target_aarch64(void)
{
    return target_arch() == SYS_EMU_TARGET_AARCH64;
}

bool target_base_ppc(void)
{
    switch (target_arch()) {
    case SYS_EMU_TARGET_PPC:
    case SYS_EMU_TARGET_PPC64:
        return true;
    default:
        return false;
    }
}

bool target_ppc(void)
{
    return target_arch() == SYS_EMU_TARGET_PPC;
}

bool target_ppc64(void)
{
    return target_arch() == SYS_EMU_TARGET_PPC64;
}

bool target_s390x(void)
{
    return target_arch() == SYS_EMU_TARGET_S390X;
}

bool target_base_riscv(void)
{
    switch (target_arch()) {
    case SYS_EMU_TARGET_RISCV32:
    case SYS_EMU_TARGET_RISCV64:
        return true;
    default:
        return false;
    }
}

bool target_riscv32(void)
{
    return target_arch() == SYS_EMU_TARGET_RISCV32;
}

bool target_riscv64(void)
{
    return target_arch() == SYS_EMU_TARGET_RISCV64;
}

bool target_config_multiprocess(void)
{
    return target_info()->config_multiprocess;
}

bool target_config_nitro(void)
{
    return target_info()->config_nitro;
}

bool target_config_xen(void)
{
    return target_info()->config_xen;
}

static TargetCpuOps target_cpu_ops;

void target_info_register_cpu_op(const char *cpu_type, size_t offset,
                                 void *impl)
{
    const TargetInfo *ti = target_info();
    void **slot;

    g_assert(ti);
    g_assert(offset + sizeof(void *) <= sizeof(TargetCpuOps));
    g_assert((offset % sizeof(void *)) == 0);

    if (strcmp(cpu_type, ti->cpu_type)) {
        return;
    }

    slot = (void **)((char *)&target_cpu_ops + offset);
    if (*slot) {
        error_report("TargetCpuOps already registered for type '%s' (offset %zu)",
                     cpu_type, offset);
        return;
    }
    *slot = impl;
}

const TargetCpuOps *target_info_cpu_ops(void)
{
    return &target_cpu_ops;
}
