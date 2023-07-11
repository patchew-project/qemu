/*
 * RISC-V CPU helpers for qemu.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
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
#include "cpu.h"
#include "internals.h"
#ifndef CONFIG_USER_ONLY
#include "sysemu/cpu-timers.h"
#endif


int riscv_cpu_mmu_index(CPURISCVState *env, bool ifetch)
{
#ifdef CONFIG_USER_ONLY
    return 0;
#else
    bool virt = env->virt_enabled;
    int mode = env->priv;

    /* All priv -> mmu_idx mapping are here */
    if (!ifetch) {
        uint64_t status = env->mstatus;

        if (mode == PRV_M && get_field(status, MSTATUS_MPRV)) {
            mode = get_field(env->mstatus, MSTATUS_MPP);
            virt = get_field(env->mstatus, MSTATUS_MPV) &&
                   (mode != PRV_M);
            if (virt) {
                status = env->vsstatus;
            }
        }
        if (mode == PRV_S && get_field(status, MSTATUS_SUM)) {
            mode = MMUIdx_S_SUM;
        }
    }

    return mode | (virt ? MMU_2STAGE_BIT : 0);
#endif
}

void riscv_cpu_update_mask(CPURISCVState *env)
{
    target_ulong mask = 0, base = 0;
    RISCVMXL xl = env->xl;
    /*
     * TODO: Current RVJ spec does not specify
     * how the extension interacts with XLEN.
     */
#ifndef CONFIG_USER_ONLY
    int mode = cpu_address_mode(env);
    xl = cpu_get_xl(env, mode);
    if (riscv_has_ext(env, RVJ)) {
        switch (mode) {
        case PRV_M:
            if (env->mmte & M_PM_ENABLE) {
                mask = env->mpmmask;
                base = env->mpmbase;
            }
            break;
        case PRV_S:
            if (env->mmte & S_PM_ENABLE) {
                mask = env->spmmask;
                base = env->spmbase;
            }
            break;
        case PRV_U:
            if (env->mmte & U_PM_ENABLE) {
                mask = env->upmmask;
                base = env->upmbase;
            }
            break;
        default:
            g_assert_not_reached();
        }
    }
#endif
    if (xl == MXL_RV32) {
        env->cur_pmmask = mask & UINT32_MAX;
        env->cur_pmbase = base & UINT32_MAX;
    } else {
        env->cur_pmmask = mask;
        env->cur_pmbase = base;
    }
}
