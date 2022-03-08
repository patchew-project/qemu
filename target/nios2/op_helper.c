/*
 * Altera Nios II helper routines.
 *
 * Copyright (C) 2012 Chris Wulff <crwulff@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "qemu/main-loop.h"

void helper_raise_exception(CPUNios2State *env, uint32_t index)
{
    CPUState *cs = env_cpu(env);
    cs->exception_index = index;
    cpu_loop_exit(cs);
}

#ifndef CONFIG_USER_ONLY
void helper_eret(CPUNios2State *env, uint32_t new_pc)
{
    Nios2CPU *cpu = env_archcpu(env);
    unsigned crs = FIELD_EX32(env->status, CR_STATUS, CRS);
    uint32_t val;

    if (crs == 0) {
        val = env->estatus;
    } else {
        val = env->shadow_regs[crs][R_SSTATUS];
    }

    /*
     * Both estatus and sstatus have no constraints on write;
     * do not allow reserved fields in status to be set.
     */
    val &= (cpu->cr_state[CR_STATUS].writable |
            cpu->cr_state[CR_STATUS].readonly);
    env->status = val;
    nios2_update_crs(env);

    env->pc = new_pc;
    cpu_loop_exit(env_cpu(env));
}

uint32_t helper_rdprs(CPUNios2State *env, uint32_t regno)
{
    unsigned prs = FIELD_EX32(env->status, CR_STATUS, PRS);
    return env->shadow_regs[prs][regno];
}

void helper_wrprs(CPUNios2State *env, uint32_t regno, uint32_t val)
{
    unsigned prs = FIELD_EX32(env->status, CR_STATUS, PRS);
    env->shadow_regs[prs][regno] = val;
}
#endif /* !CONFIG_USER_ONLY */
