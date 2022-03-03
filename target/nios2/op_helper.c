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

void helper_eret(CPUNios2State *env, uint32_t new_pc)
{
    uint32_t crs = cpu_get_crs(env);
    if (crs == 0) {
        env->regs[CR_STATUS] = env->regs[CR_ESTATUS];
    } else {
        env->regs[CR_STATUS] = env->regs[R_SSTATUS];
    }

    /*
     * At this point CRS was updated by the above assignment to CR_STATUS.
     * Therefore we need to retrieve the new value of CRS and potentially
     * switch the register set
     */
    cpu_change_reg_set(env, crs, cpu_get_crs(env));
    env->regs[R_PC] = new_pc;
}
