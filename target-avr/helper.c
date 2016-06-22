/*
 * QEMU AVR CPU
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

#include "cpu.h"
#include "hw/irq.h"
#include "include/hw/sysbus.h"
#include "include/sysemu/sysemu.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"

bool avr_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    CPUClass *cc = CPU_GET_CLASS(cs);
    AVRCPU *cpu = AVR_CPU(cs);
    CPUAVRState *env = &cpu->env;

    bool ret = false;

    if (interrupt_request & CPU_INTERRUPT_RESET) {
        if (cpu_interrupts_enabled(env)) {
            cs->exception_index = EXCP_RESET;
            cc->do_interrupt(cs);

            cs->interrupt_request   &= ~CPU_INTERRUPT_RESET;

            ret = true;
        }
    }
    if (interrupt_request & CPU_INTERRUPT_HARD) {
        if (cpu_interrupts_enabled(env) && env->intsrc != 0) {
            int     index = ctz32(env->intsrc);
            cs->exception_index = EXCP_INT(index);
            cc->do_interrupt(cs);

            env->intsrc &= env->intsrc - 1; /* clear the interrupt */
            cs->interrupt_request &= ~CPU_INTERRUPT_HARD;

            ret = true;
        }
    }
    return ret;
}

void avr_cpu_do_interrupt(CPUState *cs)
{
    AVRCPU         *cpu = AVR_CPU(cs);
    CPUAVRState    *env = &cpu->env;

    uint32_t ret = env->pc_w;
    int vector = 0;
    int size = avr_feature(env, AVR_FEATURE_JMP_CALL) ? 2 : 1;
    int base = 0;    /* TODO: where to get it */

    if (cs->exception_index == EXCP_RESET) {
        vector = 0;
    } else if (env->intsrc != 0) {
        vector = ctz32(env->intsrc) + 1;
    }

    if (avr_feature(env, AVR_FEATURE_3_BYTE_PC)) {
        stb_phys(cs->as, env->sp--, (ret & 0x0000ff));
        stb_phys(cs->as, env->sp--, (ret & 0x00ff00) >>  8);
        stb_phys(cs->as, env->sp--, (ret & 0xff0000) >> 16);
    } else if (avr_feature(env, AVR_FEATURE_2_BYTE_PC)) {
        stb_phys(cs->as, env->sp--, (ret & 0x0000ff));
        stb_phys(cs->as, env->sp--, (ret & 0x00ff00) >>  8);
    } else {
        stb_phys(cs->as, env->sp--, (ret & 0x0000ff));
    }

    env->pc_w = base + vector * size;
    env->sregI = 0;    /*  clear Global Interrupt Flag */

    cs->exception_index = -1;
}

int avr_cpu_memory_rw_debug(CPUState *cs, vaddr addr, uint8_t *buf,
                                int len, bool is_write)
{
    return  cpu_memory_rw_debug(cs, addr, buf, len, is_write);
}

hwaddr avr_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    return  addr;   /*  I assume 1:1 address correspondance */
}

int avr_cpu_handle_mmu_fault(CPUState *cs, vaddr address, int rw, int mmu_idx)
{
    cs->exception_index = EXCP_DEBUG;
    cpu_dump_state(cs, stderr, fprintf, 0);
    return 1;
}

void tlb_fill(CPUState *cs, target_ulong vaddr, int is_write,
                                int mmu_idx, uintptr_t retaddr)
{
    target_ulong page_size = TARGET_PAGE_SIZE;
    int prot = 0;
    MemTxAttrs attrs = {};
    uint32_t paddr;

    vaddr &= TARGET_PAGE_MASK;

    if (mmu_idx == MMU_CODE_IDX) {
        paddr = PHYS_BASE_CODE + vaddr;
        prot = PAGE_READ | PAGE_EXEC;
    } else {
        paddr = PHYS_BASE_DATA + vaddr;
        prot = PAGE_READ | PAGE_WRITE;
    }

    tlb_set_page_with_attrs(cs, vaddr, paddr, attrs, prot, mmu_idx, page_size);
}

void helper_debug(CPUAVRState *env)
{
    CPUState *cs = CPU(avr_env_get_cpu(env));

    cs->exception_index = EXCP_DEBUG;
    cpu_loop_exit(cs);
}

