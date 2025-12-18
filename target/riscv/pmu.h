/*
 * RISC-V PMU header file.
 *
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
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

#ifndef RISCV_PMU_H
#define RISCV_PMU_H

#include "cpu.h"
#include "qapi/error.h"

#define RISCV_PMU_CTR_IS_HPM(x) ((x) > HPM_MINSTRET_IDX)

void riscv_pmu_timer_cb(void *priv);
void riscv_pmu_init(RISCVCPU *cpu, Error **errp);
int riscv_pmu_setup_timer(CPURISCVState *env, uint64_t value,
                          uint32_t ctr_idx);
uint32_t riscv_pmu_csrno_to_ctr_idx(int csrno);
RISCVException riscv_pmu_ctr_read(CPURISCVState *env, uint32_t ctr_idx,
                                  uint64_t *val);
RISCVException riscv_pmu_ctr_write(CPURISCVState *env, uint32_t ctr_idx,
                                   uint64_t val);
RISCVException riscv_pmu_ctr_read_general(CPURISCVState *env, uint32_t ctr_idx,
                                          uint64_t *val);
RISCVException riscv_pmu_ctr_write_general(CPURISCVState *env, uint32_t ctr_idx,
                                           uint64_t val);
typedef uint64_t riscv_pmu_preserved_ctrs_t[RV_MAX_MHPMCOUNTERS];
void riscv_pmu_preserve_ctrs(CPURISCVState *env, riscv_pmu_preserved_ctrs_t data);
void riscv_pmu_restore_ctrs(CPURISCVState *env, riscv_pmu_preserved_ctrs_t data);

#endif /* RISCV_PMU_H */
