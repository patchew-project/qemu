/*
 * QEMU binary/target API (qemu-system-arm)
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/target-info-impl.h"
#include "qemu/target-info-init.h"
#include "hw/arm/machines-qom.h"
#include "target/arm/cpu-qom.h"
#include "target/arm/cpu-param.h"
#ifndef CONFIG_USER_ONLY
#include CONFIG_DEVICES
#endif

static const TargetInfo target_info_arm_system = {
    .target_name = "arm",
    .target_arch = SYS_EMU_TARGET_ARM,
    .long_bits = 32,
    .cpu_type = TYPE_ARM_CPU,
    .machine_typename = TYPE_TARGET_ARM_MACHINE,
    .endianness = ENDIAN_MODE_LITTLE,
    .page_bits_vary = true,
    .page_bits_init = TARGET_PAGE_BITS_LEGACY,

#ifndef CONFIG_USER_ONLY
# ifdef CONFIG_MULTIPROCESS
    .config_multiprocess = true,
# else
    .config_multiprocess = false,
# endif
# ifdef CONFIG_NITRO
    .config_nitro = true,
# else
    .config_nitro = false,
# endif
# ifdef CONFIG_XEN
    .config_xen = true,
# else
    .config_xen = false,
# endif
#endif
};

target_info_init(target_info_arm_system)
