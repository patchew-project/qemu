/*
 * ARM gdb server stub
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2013 SUSE LINUX Products GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#include "qemu-common.h"
#include "cpu.h"
#include "exec/gdbstub.h"
#include "exec/softmmu-arm-semi.h"

/* Old gdb always expect FPA registers.  Newer (xml-aware) gdb only expect
   whatever the target description contains.  Due to a historical mishap
   the FPA registers appear in between core integer regs and the CPSR.
   We hack round this by giving the FPA regs zero size when talking to a
   newer gdb.  */

int arm_cpu_gdb_read_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
#ifndef CONFIG_USER_ONLY
    bool targ_bigendian = arm_bswap_needed(env);
#endif

    if (n < 16) {
        /* Core integer register.  */
#ifdef CONFIG_USER_ONLY
        return gdb_get_reg32(mem_buf, env->regs[n]);
#else
        if (targ_bigendian) {
            stl_be_p(mem_buf, env->regs[n]);
        } else {
            stl_le_p(mem_buf, env->regs[n]);
        }
        return 4;
#endif
    }
    if (n < 24) {
        /* FPA registers.  */
        if (gdb_has_xml) {
            return 0;
        }
        memset(mem_buf, 0, 12);
        return 12;
    }
    switch (n) {
    case 24:
        /* FPA status register.  */
        if (gdb_has_xml) {
            return 0;
        }
#ifdef CONFIG_USER_ONLY
        return gdb_get_reg32(mem_buf, 0);
#else
        if (targ_bigendian) {
            stl_be_p(mem_buf, 0);
        } else {
            stl_le_p(mem_buf, 0);
        }
        return 4;
#endif
    case 25:
        /* CPSR */
#ifdef CONFIG_USER_ONLY
        return gdb_get_reg32(mem_buf, cpsr_read(env));
#else
        if (targ_bigendian) {
            stl_be_p(mem_buf, cpsr_read(env));
        } else {
            stl_le_p(mem_buf, cpsr_read(env));
        }
        return 4;
#endif
    }
    /* Unknown register.  */
    return 0;
}

int arm_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint32_t tmp;
#ifndef CONFIG_USER_ONLY
    bool targ_bigendian = arm_bswap_needed(env);
#endif

#ifdef CONFIG_USER_ONLY
    tmp = ldl_p(mem_buf);
#else
    if (targ_bigendian) {
        tmp = ldl_be_p(mem_buf);
    } else {
        tmp = ldl_le_p(mem_buf);
    }
#endif

    /* Mask out low bit of PC to workaround gdb bugs.  This will probably
       cause problems if we ever implement the Jazelle DBX extensions.  */
    if (n == 15) {
        tmp &= ~1;
    }

    if (n < 16) {
        /* Core integer register.  */
        env->regs[n] = tmp;
        return 4;
    }
    if (n < 24) { /* 16-23 */
        /* FPA registers (ignored).  */
        if (gdb_has_xml) {
            return 0;
        }
        return 12;
    }
    switch (n) {
    case 24:
        /* FPA status register (ignored).  */
        if (gdb_has_xml) {
            return 0;
        }
        return 4;
    case 25:
        /* CPSR */
        cpsr_write(env, tmp, 0xffffffff, CPSRWriteByGDBStub);
        return 4;
    }
    /* Unknown register.  */
    return 0;
}
