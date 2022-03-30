/*
 * QEMU RISC-V Hart Array interface
 *
 * Copyright (c) 2017 SiFive, Inc.
 *
 * Holds the state of a heterogenous array of RISC-V harts
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_RISCV_HART_H
#define HW_RISCV_HART_H

#include "hw/cpu/cpus.h"
#include "target/riscv/cpu.h"
#include "qom/object.h"

#define TYPE_RISCV_HART_ARRAY "riscv.hart_array"

OBJECT_DECLARE_SIMPLE_TYPE(RISCVHartArrayState, RISCV_HART_ARRAY)

struct RISCVHartArrayState {
    /*< private >*/
    CpusState parent_obj;

    /*< public >*/
    uint32_t hartid_base;
    uint64_t resetvec;
};

/**
 * riscv_array_get_hart:
 * Helper to get an hart from the container.
 */
static inline RISCVCPU *riscv_array_get_hart(RISCVHartArrayState *s, int i)
{
    return RISCV_CPU(CPUS(s)->cpus[i]);
}

/**
 * riscv_array_get_num_harts:
 * Helper to get the number of harts in the container.
 */
static inline unsigned riscv_array_get_num_harts(RISCVHartArrayState *s)
{
    return CPUS(s)->topology.cpus;
}

/* Temporary function until we migrated the riscv hart array to simple device */
void riscv_hart_array_realize(RISCVHartArrayState *state, Error **errp);

#endif
