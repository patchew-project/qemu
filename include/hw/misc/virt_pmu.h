/*
 * RISC-V Virt machine PMU header file.
 * Copyright (C) 2025, Syntacore Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 * Also add information on how to contact you by electronic and paper mail.
 */

#ifndef RISCV_VIRT_PMU_H
#define RISCV_VIRT_PMU_H

#include "target/riscv/cpu.h"
#include "qapi/error.h"

typedef enum riscv_sbi_pmu_types {
    RISCV_SBI_EVENT_TYPE_GEN      = 0x0,
    RISCV_SBI_EVENT_TYPE_CACHE    = 0x1,
    RISCV_SBI_EVENT_TYPE_RAW      = 0x2,
    RISCV_SBI_EVENT_TYPE_RAW_V2   = 0x3,
    RISCV_SBI_EVENT_TYPE_FIRMWARE = 0xf,
} riscv_sbi_pmu_types;

REG32(SBI_MHPMEVENT, 0x323)
    FIELD(SBI_MHPMEVENT, CODE, 0, 16)
    FIELD(SBI_MHPMEVENT, TYPE, 16, 4)

/* Generic type events */

typedef enum riscv_sbi_pmu_hw {
    SBI_PMU_HW_NO_EVENT                = 0x00000,
    SBI_PMU_HW_CPU_CYCLES              = 0x00001,
    SBI_PMU_HW_INSTRUCTIONS            = 0x00002,
    SBI_PMU_HW_CACHE_REFERENCES        = 0x00003,
    SBI_PMU_HW_CACHE_MISSES            = 0x00004,
    SBI_PMU_HW_BRANCH_INSTRUCTIONS     = 0x00005,
    SBI_PMU_HW_BRANCH_MISSES           = 0x00006,
    SBI_PMU_HW_BUS_CYCLES              = 0x00007,
    SBI_PMU_HW_STALLED_CYCLES_FRONTEND = 0x00008,
    SBI_PMU_HW_STALLED_CYCLES_BACKEND  = 0x00009,
    SBI_PMU_HW_REF_CPU_CYCLES          = 0x0000A,
} riscv_sbi_pmu_hw;

/* Cache type events */

typedef enum riscv_sbi_pmu_cache_event {
    SBI_PMU_HW_CACHE_L1D  = 0x0,
    SBI_PMU_HW_CACHE_L1I  = 0x1,
    SBI_PMU_HW_CACHE_LL   = 0x2,
    SBI_PMU_HW_CACHE_DTLB = 0x3,
    SBI_PMU_HW_CACHE_ITLB = 0x4,
    SBI_PMU_HW_CACHE_BPU  = 0x5,
    SBI_PMU_HW_CACHE_NODE = 0x6,
} riscv_sbi_pmu_cache_event;

typedef enum riscv_sbi_pmu_cache_op {
    SBI_PMU_HW_CACHE_OP_READ     = 0,
    SBI_PMU_HW_CACHE_OP_WRITE    = 1,
    SBI_PMU_HW_CACHE_OP_PREFETCH = 2,
} riscv_sbi_pmu_cache_op;

typedef enum riscv_sbi_pmu_cache_result {
    SBI_PMU_HW_CACHE_RESULT_ACCESS = 0,
    SBI_PMU_HW_CACHE_RESULT_MISS   = 1,
} riscv_sbi_pmu_cache_result;

REG32(SBI_MHPMEVENT_CACHE, 0x323)
    FIELD(SBI_MHPMEVENT_CACHE, RESULT, 0, 1)
    FIELD(SBI_MHPMEVENT_CACHE, OP, 1, 2)
    FIELD(SBI_MHPMEVENT_CACHE, EVENT, 3, 13)


void riscv_virt_pmu_generate_fdt_node(void *fdt, uint32_t cmask, char *pmu_name);
bool riscv_virt_supported_events(CPURISCVState *env, uint32_t ctr_idx);
RISCVException riscv_virt_pmu_ctr_read(CPURISCVState *env, uint32_t ctr_idx,
                                       uint64_t *value);
RISCVException riscv_virt_pmu_ctr_write(CPURISCVState *env, uint32_t ctr_idx,
                                        uint64_t value);

#endif /* RISCV_VIRT_PMU_H */
