/*
 * Xuantie implementation for RISC-V Control and Status Registers.
 *
 * Copyright (c) 2024 Alibaba Group. All rights reserved.
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
#include "cpu.h"
#include "tcg/tcg-cpu.h"
#include "exec/exec-all.h"
#include "exec/tb-flush.h"
#include "qapi/error.h"

static RISCVException th_maee_check(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_cfg(env)->ext_xtheadmaee) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    return RISCV_EXCP_NONE;
}

static RISCVException
read_th_mxstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->th_mxstatus;
    return RISCV_EXCP_NONE;
}

static RISCVException
write_th_mxstatus(CPURISCVState *env, int csrno, target_ulong val)
{
    uint64_t mxstatus = env->th_mxstatus;
    uint64_t mask = TH_MXSTATUS_MAEE;

    if ((val ^ mxstatus) & TH_MXSTATUS_MAEE) {
        tlb_flush(env_cpu(env));
    }

    mxstatus = (mxstatus & ~mask) | (val & mask);
    env->th_mxstatus = mxstatus;
    return RISCV_EXCP_NONE;
}

riscv_csr_operations th_csr_ops[CSR_TABLE_SIZE] = {
#if !defined(CONFIG_USER_ONLY)
    [CSR_TH_MXSTATUS]     = { "th_mxstatus", th_maee_check, read_th_mxstatus,
                                                            write_th_mxstatus},
#endif /* !CONFIG_USER_ONLY */
};
