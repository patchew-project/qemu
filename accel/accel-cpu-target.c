/*
 * Accelerator CPU parent registration.
 *
 * Compiled once per target. System targets for arm, riscv, sh4, and
 * xtensa share one CPU_RESOLVING_TYPE, so those bases compile it once.
 * The constructor is static, so more than one copy can be linked.
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "accel/accel-cpu-target.h"

static void register_accel_target_type(void)
{
    const TypeInfo accel_cpu_type = {
        .name = TYPE_ACCEL_CPU,
        .parent = TYPE_OBJECT,
        .abstract = true,
        .class_size = sizeof(AccelCPUClass),
    };

    type_register_static(&accel_cpu_type);
}

type_init(register_accel_target_type);
