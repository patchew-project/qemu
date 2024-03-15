/*
 * Helpers for CPU QOM types
 *
 * SPDX-FileCopyrightText: 2024 Linaro Ltd.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "sysemu/arch_init.h"

#include "target/alpha/cpu-qom.h"
#include "target/arm/cpu-qom.h"
#include "target/avr/cpu-qom.h"
#include "target/cris/cpu-qom.h"
#include "target/hexagon/cpu-qom.h"
#include "target/hppa/cpu-qom.h"
#include "target/i386/cpu-qom.h"
#include "target/loongarch/cpu-qom.h"
#include "target/m68k/cpu-qom.h"
#include "target/microblaze/cpu-qom.h"
#include "target/mips/cpu-qom.h"
#include "target/nios2/cpu-qom.h"
#include "target/openrisc/cpu-qom.h"
#include "target/riscv/cpu-qom.h"
#include "target/rx/cpu-qom.h"
#include "target/s390x/cpu-qom.h"
#include "target/sparc/cpu-qom.h"
#include "target/sh4/cpu-qom.h"
#include "target/tricore/cpu-qom.h"
#include "target/xtensa/cpu-qom.h"

const char *cpu_typename_by_arch_bit(QemuArchBit arch_bit)
{
    static const char *cpu_bit_to_typename[QEMU_ARCH_BIT_LAST + 1] = {
        [QEMU_ARCH_BIT_ALPHA]       = TYPE_ALPHA_CPU,
        [QEMU_ARCH_BIT_ARM]         = TYPE_ARM_CPU,
        [QEMU_ARCH_BIT_CRIS]        = TYPE_CRIS_CPU,
        [QEMU_ARCH_BIT_I386]        = TYPE_I386_CPU,
        [QEMU_ARCH_BIT_M68K]        = TYPE_M68K_CPU,
        [QEMU_ARCH_BIT_MICROBLAZE]  = TYPE_MICROBLAZE_CPU,
        [QEMU_ARCH_BIT_MIPS]        = TYPE_MIPS_CPU,
        /* TODO:                      TYPE_PPC_CPU */
        [QEMU_ARCH_BIT_S390X]       = TYPE_S390_CPU,
        [QEMU_ARCH_BIT_SH4]         = TYPE_SUPERH_CPU,
        [QEMU_ARCH_BIT_SPARC]       = TYPE_SPARC_CPU,
        [QEMU_ARCH_BIT_XTENSA]      = TYPE_XTENSA_CPU,
        [QEMU_ARCH_BIT_OPENRISC]    = TYPE_OPENRISC_CPU,
        [QEMU_ARCH_BIT_TRICORE]     = TYPE_TRICORE_CPU,
        [QEMU_ARCH_BIT_NIOS2]       = TYPE_NIOS2_CPU,
        [QEMU_ARCH_BIT_HPPA]        = TYPE_HPPA_CPU,
        [QEMU_ARCH_BIT_RISCV]       = TYPE_RISCV_CPU,
        [QEMU_ARCH_BIT_RX]          = TYPE_RX_CPU,
        [QEMU_ARCH_BIT_AVR]         = TYPE_AVR_CPU,
        [QEMU_ARCH_BIT_HEXAGON]     = TYPE_HEXAGON_CPU,
        [QEMU_ARCH_BIT_LOONGARCH]   = TYPE_LOONGARCH_CPU,
    };
    return cpu_bit_to_typename[arch_bit];
}
