/*
 * RISC-V Virt machine PMU emulation.
 *
 * Copyright (c) 2025 Syntacore.
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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "target/riscv/cpu.h"
#include "target/riscv/pmu.h"
#include "include/hw/misc/virt_pmu.h"
#include "system/device_tree.h"

#define SBI_CACHE_EVENT_ID_CNT (SBI_PMU_HW_CACHE_NODE - SBI_PMU_HW_CACHE_L1D + 1)
#define SBI_CACHE_OP_ID_CNT (SBI_PMU_HW_CACHE_OP_PREFETCH - SBI_PMU_HW_CACHE_OP_READ + 1)
#define SBI_EVT_CTR_SIZE (SBI_CACHE_EVENT_ID_CNT * SBI_CACHE_OP_ID_CNT + 3)

void riscv_virt_pmu_generate_fdt_node(void *fdt, uint32_t cmask, char *pmu_name)
{
    uint32_t fdt_event_mhpmctr_map[SBI_EVT_CTR_SIZE][3] = {};
    uint32_t event_mhpmctr_idx = 0;

    uint32_t event_idx_low, event_idx_high;

    /* SBI_PMU_HW_CPU_CYCLES */
    fdt_event_mhpmctr_map[event_mhpmctr_idx][0] = cpu_to_be32(SBI_PMU_HW_CPU_CYCLES);
    fdt_event_mhpmctr_map[event_mhpmctr_idx][1] = cpu_to_be32(SBI_PMU_HW_CPU_CYCLES);
    fdt_event_mhpmctr_map[event_mhpmctr_idx][2] = cpu_to_be32(cmask | 1 << 0);
    event_mhpmctr_idx++;

    /* SBI_PMU_HW_INSTRUCTIONS */
    fdt_event_mhpmctr_map[event_mhpmctr_idx][0] =
        cpu_to_be32(SBI_PMU_HW_INSTRUCTIONS);

    fdt_event_mhpmctr_map[event_mhpmctr_idx][1] =
        cpu_to_be32(SBI_PMU_HW_INSTRUCTIONS);

    fdt_event_mhpmctr_map[event_mhpmctr_idx][2] = cpu_to_be32(cmask | 1 << 2);
    event_mhpmctr_idx++;

    /* Other generic type events */
    fdt_event_mhpmctr_map[event_mhpmctr_idx][0] =
        cpu_to_be32(SBI_PMU_HW_CACHE_REFERENCES);

    fdt_event_mhpmctr_map[event_mhpmctr_idx][1] =
        cpu_to_be32(SBI_PMU_HW_REF_CPU_CYCLES);

    fdt_event_mhpmctr_map[event_mhpmctr_idx][2] = cpu_to_be32(cmask);
    event_mhpmctr_idx++;

    /* Cache type */
    for (uint32_t ev_id = 0; ev_id < SBI_CACHE_EVENT_ID_CNT; ++ev_id) {
        for (uint32_t op_id = 0; op_id < SBI_CACHE_OP_ID_CNT; ++op_id) {
            event_idx_low = FIELD_DP32(0, SBI_MHPMEVENT_CACHE, OP, op_id);
            event_idx_low = FIELD_DP32(event_idx_low, SBI_MHPMEVENT_CACHE, EVENT, ev_id);
            event_idx_low = FIELD_DP32(event_idx_low, SBI_MHPMEVENT, TYPE,
                                       RISCV_SBI_EVENT_TYPE_CACHE);
            event_idx_low = FIELD_DP32(event_idx_low, SBI_MHPMEVENT_CACHE, RESULT,
                                       SBI_PMU_HW_CACHE_RESULT_ACCESS);

            event_idx_high = FIELD_DP32(event_idx_low, SBI_MHPMEVENT_CACHE, RESULT,
                                        SBI_PMU_HW_CACHE_RESULT_MISS);

            fdt_event_mhpmctr_map[event_mhpmctr_idx][0] = cpu_to_be32(event_idx_low);
            fdt_event_mhpmctr_map[event_mhpmctr_idx][1] = cpu_to_be32(event_idx_high);
            fdt_event_mhpmctr_map[event_mhpmctr_idx][2] = cpu_to_be32(cmask);
            event_mhpmctr_idx++;
        }
    }

    g_assert(event_mhpmctr_idx <= SBI_EVT_CTR_SIZE);

    /* This a OpenSBI specific DT property documented in OpenSBI docs */
    qemu_fdt_setprop(fdt, pmu_name, "riscv,event-to-mhpmcounters",
                     fdt_event_mhpmctr_map,
                     event_mhpmctr_idx * sizeof(fdt_event_mhpmctr_map[0]));
}

bool riscv_virt_supported_events(CPURISCVState *env, uint32_t ctr_idx)
{
    uint32_t sbi_event_type = FIELD_EX32(env->mhpmevent_val[ctr_idx],
                                         SBI_MHPMEVENT, TYPE);
    uint32_t sbi_cache_event;
    uint32_t sbi_cache_op;
    uint32_t sbi_cache_result;

    switch (sbi_event_type) {
    case RISCV_SBI_EVENT_TYPE_GEN:
        return sbi_event_type <= SBI_PMU_HW_REF_CPU_CYCLES;
    case RISCV_SBI_EVENT_TYPE_CACHE:
        sbi_cache_event = FIELD_EX32(sbi_event_type, SBI_MHPMEVENT_CACHE, EVENT);
        sbi_cache_op = FIELD_EX32(sbi_event_type, SBI_MHPMEVENT_CACHE, OP);
        sbi_cache_result = FIELD_EX32(sbi_event_type, SBI_MHPMEVENT_CACHE, RESULT);

        return sbi_cache_event <= SBI_PMU_HW_CACHE_NODE &&
               sbi_cache_op <= SBI_PMU_HW_CACHE_OP_PREFETCH &&
               sbi_cache_result <= SBI_PMU_HW_CACHE_RESULT_MISS;
    default:
        return false;
    }
}

RISCVException riscv_virt_pmu_ctr_read(CPURISCVState *env, uint32_t ctr_idx,
                                       uint64_t *value)
{
    uint32_t sbi_event_type = FIELD_EX32(env->mhpmevent_val[ctr_idx],
                                         SBI_MHPMEVENT, TYPE);

    switch (sbi_event_type) {
    /* If we want to handle some events separately */
    default:
    /* In case we do not want handle it separately */
        return riscv_pmu_ctr_read_general(env, ctr_idx, value);
    }
}

RISCVException riscv_virt_pmu_ctr_write(CPURISCVState *env, uint32_t ctr_idx,
                                        uint64_t value)
{
    uint32_t sbi_event_type = FIELD_EX32(env->mhpmevent_val[ctr_idx],
                                         SBI_MHPMEVENT, TYPE);

    switch (sbi_event_type) {
    /* If we want to handle some events separately */
    default:
    /* In case we do not want handle it separately */
        return riscv_pmu_ctr_write_general(env, ctr_idx, value);
    }
}
