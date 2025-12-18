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

void riscv_pmu_timer_cb(void *priv);
void riscv_pmu_init(RISCVCPU *cpu, Error **errp);
void riscv_pmu_generate_fdt_node(void *fdt, uint32_t cmask, char *pmu_name);
int riscv_pmu_setup_timer(CPURISCVState *env, uint64_t value,
                          uint32_t ctr_idx);
void riscv_pmu_update_fixed_ctrs(CPURISCVState *env, target_ulong newpriv,
                                 bool new_virt);
RISCVException riscv_pmu_read_ctr(CPURISCVState *env, target_ulong *val,
                                  bool upper_half, uint32_t ctr_idx);
uint32_t riscv_pmu_csrno_to_ctr_idx(int csrno);

#endif /* RISCV_PMU_H */
