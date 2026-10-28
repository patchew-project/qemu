/*
 * Per-target accelerator CPU parent registration.
 *
 * Compiled once per target. Must not define a global symbol, so each
 * copy can be linked into one binary.
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "accel/accel-cpu-target.h"
#include "accel-internal.h"

static void register_accel_target_type(void)
{
    accel_register_cpu_type(glue(SYS_EMU_TARGET_, TARGET_ARCH_VARIANT),
                            CPU_RESOLVING_TYPE);
}

type_init(register_accel_target_type);
