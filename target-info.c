/*
 * QEMU target info helpers
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/target-info.h"
#include "qemu/target-info-qapi.h"
#include "qemu/target-info-impl.h"
#include "qapi/error.h"
#include "system/arch_init.h"

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
    SysEmuTarget arch = target_info()->target_arch;

    if (arch == SYS_EMU_TARGET__MAX) {
        arch = qapi_enum_parse(&SysEmuTarget_lookup, target_name(), -1,
                               &error_abort);
    }
    return arch;
}

bool qemu_arch_available(unsigned qemu_arch_mask)
{
    switch (target_arch()) {
    case SYS_EMU_TARGET_ALPHA:
        return qemu_arch_mask & QEMU_ARCH_ALPHA;
    case SYS_EMU_TARGET_ARM:
    case SYS_EMU_TARGET_AARCH64:
        return qemu_arch_mask & QEMU_ARCH_ARM;
    case SYS_EMU_TARGET_I386:
    case SYS_EMU_TARGET_X86_64:
        return qemu_arch_mask & QEMU_ARCH_I386;
    case SYS_EMU_TARGET_M68K:
        return qemu_arch_mask & QEMU_ARCH_M68K;
    case SYS_EMU_TARGET_MICROBLAZE:
    case SYS_EMU_TARGET_MICROBLAZEEL:
        return qemu_arch_mask & QEMU_ARCH_MICROBLAZE;
    case SYS_EMU_TARGET_MIPS:
    case SYS_EMU_TARGET_MIPSEL:
    case SYS_EMU_TARGET_MIPS64:
    case SYS_EMU_TARGET_MIPS64EL:
        return qemu_arch_mask & QEMU_ARCH_MIPS;
    case SYS_EMU_TARGET_PPC:
    case SYS_EMU_TARGET_PPC64:
        return qemu_arch_mask & QEMU_ARCH_PPC;
    case SYS_EMU_TARGET_S390X:
        return qemu_arch_mask & QEMU_ARCH_S390X;
    case SYS_EMU_TARGET_SH4:
    case SYS_EMU_TARGET_SH4EB:
        return qemu_arch_mask & QEMU_ARCH_SH4;
    case SYS_EMU_TARGET_SPARC:
    case SYS_EMU_TARGET_SPARC64:
        return qemu_arch_mask & QEMU_ARCH_SPARC;
    case SYS_EMU_TARGET_XTENSA:
    case SYS_EMU_TARGET_XTENSAEB:
        return qemu_arch_mask & QEMU_ARCH_XTENSA;
    case SYS_EMU_TARGET_OR1K:
        return qemu_arch_mask & QEMU_ARCH_OPENRISC;
    case SYS_EMU_TARGET_TRICORE:
        return qemu_arch_mask & QEMU_ARCH_TRICORE;
    case SYS_EMU_TARGET_HPPA:
        return qemu_arch_mask & QEMU_ARCH_HPPA;
    case SYS_EMU_TARGET_RISCV32:
    case SYS_EMU_TARGET_RISCV64:
        return qemu_arch_mask & QEMU_ARCH_RISCV;
    case SYS_EMU_TARGET_RX:
        return qemu_arch_mask & QEMU_ARCH_RX;
    case SYS_EMU_TARGET_AVR:
        return qemu_arch_mask & QEMU_ARCH_AVR;
    /*
    case SYS_EMU_TARGET_HEXAGON:
        return qemu_arch_mask & QEMU_ARCH_HEXAGON;
    */
    case SYS_EMU_TARGET_LOONGARCH64:
        return qemu_arch_mask & QEMU_ARCH_LOONGARCH;
    default:
        g_assert_not_reached();
    };
}

const char *target_cpu_type(void)
{
    return target_info()->cpu_type;
}

const char *target_machine_typename(void)
{
    return target_info()->machine_typename;
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
