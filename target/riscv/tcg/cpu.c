/*
 * RISC-V CPU helpers (TCG specific)
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#ifndef CONFIG_USER_ONLY
#include "sysemu/cpu-timers.h"
#endif

void cpu_get_tb_cpu_state(CPURISCVState *env, vaddr *pc,
                          uint64_t *cs_base, uint32_t *pflags)
{
    CPUState *cs = env_cpu(env);
    RISCVCPU *cpu = RISCV_CPU(cs);
    RISCVExtStatus fs, vs;
    uint32_t flags = 0;

    *pc = env->xl == MXL_RV32 ? env->pc & UINT32_MAX : env->pc;
    *cs_base = 0;

    if (cpu->cfg.ext_zve32f) {
        /*
         * If env->vl equals to VLMAX, we can use generic vector operation
         * expanders (GVEC) to accerlate the vector operations.
         * However, as LMUL could be a fractional number. The maximum
         * vector size can be operated might be less than 8 bytes,
         * which is not supported by GVEC. So we set vl_eq_vlmax flag to true
         * only when maxsz >= 8 bytes.
         */
        uint32_t vlmax = vext_get_vlmax(cpu, env->vtype);
        uint32_t sew = FIELD_EX64(env->vtype, VTYPE, VSEW);
        uint32_t maxsz = vlmax << sew;
        bool vl_eq_vlmax = (env->vstart == 0) && (vlmax == env->vl) &&
                           (maxsz >= 8);
        flags = FIELD_DP32(flags, TB_FLAGS, VILL, env->vill);
        flags = FIELD_DP32(flags, TB_FLAGS, SEW, sew);
        flags = FIELD_DP32(flags, TB_FLAGS, LMUL,
                           FIELD_EX64(env->vtype, VTYPE, VLMUL));
        flags = FIELD_DP32(flags, TB_FLAGS, VL_EQ_VLMAX, vl_eq_vlmax);
        flags = FIELD_DP32(flags, TB_FLAGS, VTA,
                           FIELD_EX64(env->vtype, VTYPE, VTA));
        flags = FIELD_DP32(flags, TB_FLAGS, VMA,
                           FIELD_EX64(env->vtype, VTYPE, VMA));
        flags = FIELD_DP32(flags, TB_FLAGS, VSTART_EQ_ZERO, env->vstart == 0);
    } else {
        flags = FIELD_DP32(flags, TB_FLAGS, VILL, 1);
    }

#ifdef CONFIG_USER_ONLY
    fs = EXT_STATUS_DIRTY;
    vs = EXT_STATUS_DIRTY;
#else
    flags = FIELD_DP32(flags, TB_FLAGS, PRIV, env->priv);

    flags |= cpu_mmu_index(env, 0);
    fs = get_field(env->mstatus, MSTATUS_FS);
    vs = get_field(env->mstatus, MSTATUS_VS);

    if (env->virt_enabled) {
        flags = FIELD_DP32(flags, TB_FLAGS, VIRT_ENABLED, 1);
        /*
         * Merge DISABLED and !DIRTY states using MIN.
         * We will set both fields when dirtying.
         */
        fs = MIN(fs, get_field(env->mstatus_hs, MSTATUS_FS));
        vs = MIN(vs, get_field(env->mstatus_hs, MSTATUS_VS));
    }

    /* With Zfinx, floating point is enabled/disabled by Smstateen. */
    if (!riscv_has_ext(env, RVF)) {
        fs = (smstateen_acc_ok(env, 0, SMSTATEEN0_FCSR) == RISCV_EXCP_NONE)
             ? EXT_STATUS_DIRTY : EXT_STATUS_DISABLED;
    }

    if (cpu->cfg.debug && !icount_enabled()) {
        flags = FIELD_DP32(flags, TB_FLAGS, ITRIGGER, env->itrigger_enabled);
    }
#endif

    flags = FIELD_DP32(flags, TB_FLAGS, FS, fs);
    flags = FIELD_DP32(flags, TB_FLAGS, VS, vs);
    flags = FIELD_DP32(flags, TB_FLAGS, XL, env->xl);
    flags = FIELD_DP32(flags, TB_FLAGS, AXL, cpu_address_xl(env));
    if (env->cur_pmmask != 0) {
        flags = FIELD_DP32(flags, TB_FLAGS, PM_MASK_ENABLED, 1);
    }
    if (env->cur_pmbase != 0) {
        flags = FIELD_DP32(flags, TB_FLAGS, PM_BASE_ENABLED, 1);
    }

    *pflags = flags;
}
