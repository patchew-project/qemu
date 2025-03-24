/*
 * MicroBlaze gdb server stub
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2013 SUSE LINUX Products GmbH
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "gdbstub/registers.h"

/*
 * GDB expects SREGs in the following order:
 * PC, MSR, EAR, ESR, FSR, BTR, EDR, PID, ZPR, TLBX, TLBSX, TLBLO, TLBHI.
 *
 * PID, ZPR, TLBx, TLBsx, TLBLO, and TLBHI aren't modeled, so we don't
 * map them to anything and return a value of 0 instead.
 */

enum {
    GDB_PC    = 32 + 0,
    GDB_MSR   = 32 + 1,
    GDB_EAR   = 32 + 2,
    GDB_ESR   = 32 + 3,
    GDB_FSR   = 32 + 4,
    GDB_BTR   = 32 + 5,
    GDB_PVR0  = 32 + 6,
    GDB_PVR11 = 32 + 17,
    GDB_EDR   = 32 + 18,
};

enum {
    GDB_SP_SHL,
    GDB_SP_SHR,
};

int mb_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUMBState *env = &cpu->env;
    MemOp mo = mb_cpu_is_big_endian(cs) ? MO_BE : MO_LE;
    uint32_t msr;

    switch (n) {
    case 1 ... 31:
        return gdb_get_reg32_value(mo | MO_32, mem_buf, &env->regs[n]);
    case GDB_PC:
        return gdb_get_reg32_value(mo | MO_32, mem_buf, &env->pc);
    case GDB_MSR:
        msr = mb_cpu_read_msr(env);
        return gdb_get_reg32_value(mo | MO_32, mem_buf, &msr);
    case GDB_EAR:
#if TARGET_LONG_BITS == 64
        return gdb_get_reg64_value(mo | MO_64, mem_buf, &env->ear);
#else
        return gdb_get_reg32_value(mo | MO_32, mem_buf, &env->ear);
#endif
    case GDB_ESR:
        return gdb_get_reg32_value(mo | MO_32, mem_buf, &env->esr);
    case GDB_FSR:
        return gdb_get_reg32_value(mo | MO_32, mem_buf, &env->fsr);
    case GDB_BTR:
        return gdb_get_reg32_value(mo | MO_32, mem_buf, &env->btr);
    case GDB_PVR0 ... GDB_PVR11:
        /* PVR12 is intentionally skipped */
        return gdb_get_reg32_value(mo | MO_32, mem_buf,
                                      &cpu->cfg.pvr_regs[n - GDB_PVR0]);
    case GDB_EDR:
        return gdb_get_reg32_value(mo | MO_32, mem_buf, &env->edr);
    default:
        /* Other SRegs aren't modeled, so report a value of 0 */
        return 0;
    }
}

int mb_cpu_gdb_read_stack_protect(CPUState *cs, GByteArray *mem_buf, int n)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUMBState *env = &cpu->env;
    MemOp mo = TARGET_BIG_ENDIAN ? MO_BEUL : MO_LEUL;

    switch (n) {
    case GDB_SP_SHL:
        return gdb_get_reg32_value(mo, mem_buf, &env->slr);
        break;
    case GDB_SP_SHR:
        return gdb_get_reg32_value(mo, mem_buf, &env->shr);
        break;
    default:
        return 0;
    }
}

int mb_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    CPUMBState *env = cpu_env(cs);
    uint32_t tmp;

    tmp = ldl_p(mem_buf);

    switch (n) {
    case 1 ... 31:
        env->regs[n] = tmp;
        break;
    case GDB_PC:
        env->pc = tmp;
        break;
    case GDB_MSR:
        mb_cpu_write_msr(env, tmp);
        break;
    case GDB_EAR:
        env->ear = tmp;
        break;
    case GDB_ESR:
        env->esr = tmp;
        break;
    case GDB_FSR:
        env->fsr = tmp;
        break;
    case GDB_BTR:
        env->btr = tmp;
        break;
    case GDB_EDR:
        env->edr = tmp;
        break;
    }
    return 4;
}

int mb_cpu_gdb_write_stack_protect(CPUState *cs, uint8_t *mem_buf, int n)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUMBState *env = &cpu->env;

    switch (n) {
    case GDB_SP_SHL:
        env->slr = ldl_p(mem_buf);
        break;
    case GDB_SP_SHR:
        env->shr = ldl_p(mem_buf);
        break;
    default:
        return 0;
    }
    return 4;
}
