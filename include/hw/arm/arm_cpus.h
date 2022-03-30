/*
 * ARM CPUs
 *
 * Copyright (c) 2022 Greensocs
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_CPUS_H
#define HW_ARM_CPUS_H

#include "hw/cpu/cpus.h"
#include "target/arm/cpu.h"

#define TYPE_ARM_CPUS "arm-cpus"
OBJECT_DECLARE_SIMPLE_TYPE(ArmCpusState, ARM_CPUS)

/**
 * ArmCpusState:
 * @reset_hivecs: use to initialize cpu's reset-hivecs
 * @has_el3: use to initialize cpu's has_el3
 * @has_el2: use to initialize cpu's has_el2
 * @reset_cbar: use to initialize cpu's reset-cbar
 */
struct ArmCpusState {
    CpusState parent_obj;

    bool reset_hivecs;
    bool has_el3;
    bool has_el2;
    uint64_t reset_cbar;
};

/*
 * arm_cpus_get_cpu:
 * Helper to get an ArmCpu from the container.
 */
static inline ARMCPU *arm_cpus_get_cpu(ArmCpusState *s, unsigned i)
{
    return ARM_CPU(CPUS(s)->cpus[i]);
}

#endif /* HW_ARM_CPUS_H */
