/*
 * RISC-V Zc* extension Helpers for QEMU.
 *
 * Copyright (c) 2021-2022 PLCT Lab
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
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"

#define X_S0    8
#define X_S1    9
#define X_Sn    16
#define X_RA    1
#define X_A0    10
#define X_S4_E  7
#define X_S3_E  6
#define X_S2_E  14

static inline void update_push_pop_list(target_ulong rlist, bool *xreg_list)
{
    switch (rlist) {
    case 15:
        xreg_list[X_Sn + 11] = true;
        xreg_list[X_Sn + 10] = true;
        /* FALL THROUGH */
    case 14:
        xreg_list[X_Sn + 9] = true;
        /* FALL THROUGH */
    case 13:
        xreg_list[X_Sn + 8] = true;
        /* FALL THROUGH */
    case 12:
        xreg_list[X_Sn + 7] = true;
        /* FALL THROUGH */
    case 11:
        xreg_list[X_Sn + 6] = true;
        /* FALL THROUGH */
    case 10:
        xreg_list[X_Sn + 5] = true;
        /* FALL THROUGH */
    case 9:
        xreg_list[X_Sn + 4] = true;
        /* FALL THROUGH */
    case 8:
        xreg_list[X_Sn + 3] = true;
        /* FALL THROUGH */
    case 7:
        xreg_list[X_Sn + 2] = true;
        /* FALL THROUGH */
    case 6:
        xreg_list[X_S1] = true;
        /* FALL THROUGH */
    case 5:
        xreg_list[X_S0] = true;
        /* FALL THROUGH */
    case 4:
        xreg_list[X_RA] = true;
        break;
    }
}

static inline target_ulong caculate_stack_adj(int bytes, target_ulong rlist,
                                              target_ulong spimm)
{
    target_ulong stack_adj_base = 0;
    switch (rlist) {
    case 15:
        stack_adj_base = bytes == 4 ? 64 : 112;
        break;
    case 14:
        stack_adj_base = bytes == 4 ? 48 : 96;
        break;
    case 13:
    case 12:
        stack_adj_base = bytes == 4 ? 48 : 80;
        break;
    case 11:
    case 10:
        stack_adj_base = bytes == 4 ? 32 : 64;
        break;
    case 9:
    case 8:
        stack_adj_base = bytes == 4 ? 32 : 48;
        break;
    case 7:
    case 6:
        stack_adj_base = bytes == 4 ? 16 : 32;
        break;
    case 5:
    case 4:
        stack_adj_base = 16;
        break;
    }

    return stack_adj_base + spimm;
}

static inline target_ulong zcmp_pop(CPURISCVState *env, target_ulong sp,
                                    target_ulong rlist, target_ulong spimm,
                                    bool ret_val, bool ret)
{
    bool xreg_list[32] = {false};
    target_ulong addr;
    target_ulong stack_adj;
    int bytes = riscv_cpu_xlen(env) == 32 ? 4 : 8;
    int i;

    update_push_pop_list(rlist, xreg_list);
    stack_adj = caculate_stack_adj(bytes, rlist, spimm);
    addr = sp + stack_adj - bytes;
    for (i = 31; i >= 0; i--) {
        if (xreg_list[i]) {
            switch (bytes) {
            case 4:
                env->gpr[i] = cpu_ldl_le_data(env, addr);
                break;
            case 8:
                env->gpr[i] = cpu_ldq_le_data(env, addr);
                break;
            default:
                break;
            }
            addr -= bytes;
        }
    }

    if (ret_val) {
        env->gpr[xA0] = 0;
    }

    env->gpr[xSP] = sp + stack_adj;
    if (ret) {
        return env->gpr[xRA];
    } else {
        return env->pc;
    }
}

static inline void zcmp_push(CPURISCVState *env, target_ulong sp,
                             target_ulong rlist, target_ulong spimm)
{
    target_ulong addr = sp;
    bool xreg_list[32] = {false};
    target_ulong stack_adj;
    int bytes = riscv_cpu_xlen(env) == 32 ? 4 : 8;
    int i;

    update_push_pop_list(rlist, xreg_list);
    stack_adj = caculate_stack_adj(bytes, rlist, spimm);
    addr -= bytes;
    for (i = 31; i >= 0; i--) {
        if (xreg_list[i]) {
            switch (bytes) {
            case 4:
                cpu_stl_le_data(env, addr, env->gpr[i]);
                break;
            case 8:
                cpu_stq_le_data(env, addr, env->gpr[i]);
                break;
            default:
                break;
            }
            addr -= bytes;
        }
    }
    env->gpr[xSP] = sp - stack_adj;
}

void HELPER(cm_push)(CPURISCVState *env, target_ulong sp, target_ulong spimm,
                     target_ulong rlist)
{
    return zcmp_push(env, sp, rlist, spimm);
}

target_ulong HELPER(cm_pop)(CPURISCVState *env, target_ulong sp,
                            target_ulong spimm, target_ulong rlist)
{
    return zcmp_pop(env, sp, rlist, spimm, false, false);
}

target_ulong HELPER(cm_popret)(CPURISCVState *env, target_ulong sp,
                               target_ulong spimm, target_ulong rlist)
{
    return zcmp_pop(env, sp, rlist, spimm, false, true);
}

target_ulong HELPER(cm_popretz)(CPURISCVState *env, target_ulong sp,
                                target_ulong spimm, target_ulong rlist)
{
    return zcmp_pop(env, sp, rlist, spimm, true, true);
}
#undef X_S0
#undef X_Sn
#undef ZCMP_POP
#undef ZCMP_PUSH
