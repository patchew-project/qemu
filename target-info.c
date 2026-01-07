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
    static const unsigned base_arch_mask[SYS_EMU_TARGET__MAX] = {
        [SYS_EMU_TARGET_AARCH64]        = QEMU_ARCH_ARM,
        [SYS_EMU_TARGET_ALPHA]          = QEMU_ARCH_ALPHA,
        [SYS_EMU_TARGET_ARM]            = QEMU_ARCH_ARM,
        [SYS_EMU_TARGET_AVR]            = QEMU_ARCH_AVR,
        /*
        [SYS_EMU_TARGET_HEXAGON]        = QEMU_ARCH_HEXAGON,
        */
        [SYS_EMU_TARGET_HPPA]           = QEMU_ARCH_HPPA,
        [SYS_EMU_TARGET_I386]           = QEMU_ARCH_I386,
        [SYS_EMU_TARGET_LOONGARCH64]    = QEMU_ARCH_LOONGARCH,
        [SYS_EMU_TARGET_M68K]           = QEMU_ARCH_M68K,
        [SYS_EMU_TARGET_MICROBLAZE]     = QEMU_ARCH_MICROBLAZE,
        [SYS_EMU_TARGET_MICROBLAZEEL]   = QEMU_ARCH_MICROBLAZE,
        [SYS_EMU_TARGET_MIPS]           = QEMU_ARCH_MIPS,
        [SYS_EMU_TARGET_MIPS64]         = QEMU_ARCH_MIPS,
        [SYS_EMU_TARGET_MIPS64EL]       = QEMU_ARCH_MIPS,
        [SYS_EMU_TARGET_MIPSEL]         = QEMU_ARCH_MIPS,
        [SYS_EMU_TARGET_OR1K]           = QEMU_ARCH_OPENRISC,
        [SYS_EMU_TARGET_PPC]            = QEMU_ARCH_PPC,
        [SYS_EMU_TARGET_PPC64]          = QEMU_ARCH_PPC,
        [SYS_EMU_TARGET_RISCV32]        = QEMU_ARCH_RISCV,
        [SYS_EMU_TARGET_RISCV64]        = QEMU_ARCH_RISCV,
        [SYS_EMU_TARGET_RX]             = QEMU_ARCH_RX,
        [SYS_EMU_TARGET_S390X]          = QEMU_ARCH_S390X,
        [SYS_EMU_TARGET_SH4]            = QEMU_ARCH_SH4,
        [SYS_EMU_TARGET_SH4EB]          = QEMU_ARCH_SH4,
        [SYS_EMU_TARGET_SPARC]          = QEMU_ARCH_SPARC,
        [SYS_EMU_TARGET_SPARC64]        = QEMU_ARCH_SPARC,
        [SYS_EMU_TARGET_TRICORE]        = QEMU_ARCH_TRICORE,
        [SYS_EMU_TARGET_X86_64]         = QEMU_ARCH_I386,
        [SYS_EMU_TARGET_XTENSA]         = QEMU_ARCH_XTENSA,
        [SYS_EMU_TARGET_XTENSAEB]       = QEMU_ARCH_XTENSA,
    };

    return qemu_arch_mask & base_arch_mask[target_arch()];
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
