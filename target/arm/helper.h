/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef HELPER__H
#define HELPER__H

#include <stdint.h>
#include <stdbool.h>
#include "exec/helper-proto-common.h"
#include "exec/helper-gen-common.h"
#include "target/arm/syndrome.h"

/*
 * Callback ops for per-register access during MMIO emulation.
 * Each hypervisor backend provides its own implementation to avoid
 * syncing the full CPU state for a single-register MMIO access.
 */
struct arm_emul_ops {
    uint64_t (*get_reg)(CPUState *cpu, int reg);
    void (*set_reg)(CPUState *cpu, int reg, uint64_t val);
};

int arm_emulate_mmio(CPUState *cpu, EsrEl2 syndrome, uint64_t gpa,
                     const struct arm_emul_ops *ops);

#define HELPER_H "tcg/helper-defs.h"
#include "exec/helper-proto.h.inc"
#include "exec/helper-gen.h.inc"
#undef HELPER_H

#endif /* HELPER__H */
