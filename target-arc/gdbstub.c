/*
 * QEMU ARC CPU
 *
 * Copyright (c) 2016 Michael Rolnik
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
#include "qemu-common.h"
#include "exec/gdbstub.h"

static uint32_t arc_cpu_get_stat32(CPUState *cs)
{
    ARCCPU *cpu = ARC_CPU(cs);
    CPUARCState *env = &cpu->env;
    uint32_t val = 0;

    val |= env->stat.Hf  ? BIT(0)  : 0;
    val |= env->stat.E1f ? BIT(1)  : 0;
    val |= env->stat.E2f ? BIT(2)  : 0;
    val |= env->stat.A1f ? BIT(3)  : 0;
    val |= env->stat.A2f ? BIT(4)  : 0;
    val |= env->stat.AEf ? BIT(5)  : 0;
    val |= env->stat.DEf ? BIT(6)  : 0;
    val |= env->stat.Uf  ? BIT(7)  : 0;
    val |= env->stat.Vf  ? BIT(8)  : 0;
    val |= env->stat.Cf  ? BIT(9)  : 0;
    val |= env->stat.Nf  ? BIT(10) : 0;
    val |= env->stat.Zf  ? BIT(11) : 0;
    val |= env->stat.Lf  ? BIT(12) : 0;

    return val;
}

static void arc_cpu_set_stat32(CPUState *cs, uint32_t val)
{
    ARCCPU *cpu = ARC_CPU(cs);
    CPUARCState *env = &cpu->env;

    env->stat.Hf  = 0 != (val & BIT(0));
    env->stat.E1f = 0 != (val & BIT(1));
    env->stat.E2f = 0 != (val & BIT(2));
    env->stat.A1f = 0 != (val & BIT(3));
    env->stat.A2f = 0 != (val & BIT(4));
    env->stat.AEf = 0 != (val & BIT(5));
    env->stat.DEf = 0 != (val & BIT(6));
    env->stat.Uf  = 0 != (val & BIT(7));
    env->stat.Vf  = 0 != (val & BIT(8));
    env->stat.Cf  = 0 != (val & BIT(9));
    env->stat.Nf  = 0 != (val & BIT(10));
    env->stat.Zf  = 0 != (val & BIT(11));
    env->stat.Lf  = 0 != (val & BIT(12));
}

int arc_cpu_gdb_read_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    ARCCPU *cpu = ARC_CPU(cs);
    CPUARCState *env = &cpu->env;
    uint32_t val = 0;

    switch (n) {
        case 0x00 ... 0x3f: {
            val = env->r[n];
            break;
        }

        case 0x40: {
            val = env->pc;
            break;
        }

        case 0x41: {
            val = env->lps;
            break;
        }

        case 0x42: {
            val = env->lpe;
            break;
        }

        case 0x43: {
            val = arc_cpu_get_stat32(cs);
            break;
        }
    }

    return gdb_get_reg32(mem_buf, val);
}

int arc_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    ARCCPU *cpu = ARC_CPU(cs);
    CPUARCState *env = &cpu->env;
    uint16_t val = ldl_p(mem_buf);

    switch (n) {
        case 0x00 ... 0x3f: {
            env->r[n] = val;
            break;
        }

        case 0x40: {
            env->pc = val;
            break;
        }

        case 0x41: {
            env->lps = val;
            break;
        }

        case 0x42: {
            env->lpe = val;
            break;
        }

        case 0x43: {
            arc_cpu_set_stat32(cs, val);
            break;
        }
    }

    return 4;
}
