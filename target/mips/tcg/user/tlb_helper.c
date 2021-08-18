/*
 * MIPS TLB (Translation lookaside buffer) helpers.
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
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
#include "exec/exec-all.h"
#include "internal.h"

static void raise_mmu_exception(CPUMIPSState *env, target_ulong address,
                                MMUAccessType access_type)
{
    CPUState *cs = env_cpu(env);
    int error_code = 0;
    int flags;

    if (access_type == MMU_INST_FETCH) {
        error_code |= EXCP_INST_NOTAVAIL;
    }

    flags = page_get_flags(address);
    if (!(flags & PAGE_VALID)) {
        error_code |= EXCP_TLB_NOMATCH;
    }

    cs->exception_index = (access_type == MMU_DATA_STORE
                           ? EXCP_TLBS : EXCP_TLBL);

    env->error_code = error_code;
    env->CP0_BadVAddr = address;
}

bool mips_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                       MMUAccessType access_type, int mmu_idx,
                       bool probe, uintptr_t retaddr)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;

    /* data access */
    raise_mmu_exception(env, address, access_type);
    do_raise_exception_err(env, cs->exception_index, env->error_code, retaddr);
}

void mips_cpu_do_interrupt(CPUState *cs)
{
    cs->exception_index = EXCP_NONE;
}
