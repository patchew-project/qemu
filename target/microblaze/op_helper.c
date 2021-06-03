/*
 *  Microblaze helper routines.
 *
 *  Copyright (c) 2009 Edgar E. Iglesias <edgar.iglesias@gmail.com>.
 *  Copyright (c) 2009-2012 PetaLogix Qld Pty Ltd.
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
#include "exec/helper-proto.h"
#include "qemu/log.h"
#include "exec/exec-all.h"

void helper_put(uint32_t id, uint32_t ctrl, uint32_t data)
{
    int test = ctrl & STREAM_TEST;
    int atomic = ctrl & STREAM_ATOMIC;
    int control = ctrl & STREAM_CONTROL;
    int nonblock = ctrl & STREAM_NONBLOCK;
    int exception = ctrl & STREAM_EXCEPTION;

    qemu_log_mask(LOG_UNIMP, "Unhandled stream put to stream-id=%d data=%x %s%s%s%s%s\n",
             id, data,
             test ? "t" : "",
             nonblock ? "n" : "",
             exception ? "e" : "",
             control ? "c" : "",
             atomic ? "a" : "");
}

uint32_t helper_get(uint32_t id, uint32_t ctrl)
{
    int test = ctrl & STREAM_TEST;
    int atomic = ctrl & STREAM_ATOMIC;
    int control = ctrl & STREAM_CONTROL;
    int nonblock = ctrl & STREAM_NONBLOCK;
    int exception = ctrl & STREAM_EXCEPTION;

    qemu_log_mask(LOG_UNIMP, "Unhandled stream get from stream-id=%d %s%s%s%s%s\n",
             id,
             test ? "t" : "",
             nonblock ? "n" : "",
             exception ? "e" : "",
             control ? "c" : "",
             atomic ? "a" : "");
    return 0xdead0000 | id;
}

void helper_raise_exception(CPUMBState *env, uint32_t index)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = index;
    cpu_loop_exit(cs);
}

uint32_t helper_pcmpbf(uint32_t a, uint32_t b)
{
    unsigned int i;
    uint32_t mask = 0xff000000;

    for (i = 0; i < 4; i++) {
        if ((a & mask) == (b & mask))
            return i + 1;
        mask >>= 8;
    }
    return 0;
}

void helper_stackprot(CPUMBState *env, target_ulong addr)
{
    if (addr < env->slr || addr > env->shr) {
        CPUState *cs = env_cpu(env);

        qemu_log_mask(CPU_LOG_INT, "Stack protector violation at "
                      TARGET_FMT_lx " %x %x\n",
                      addr, env->slr, env->shr);

        env->ear = addr;
        env->esr = ESR_EC_STACKPROT;
        cs->exception_index = EXCP_HW_EXCP;
        cpu_loop_exit_restore(cs, GETPC());
    }
}

#if !defined(CONFIG_USER_ONLY)
/* Writes/reads to the MMU's special regs end up here.  */
uint32_t helper_mmu_read(CPUMBState *env, uint32_t ext, uint32_t rn)
{
    return mmu_read(env, ext, rn);
}

void helper_mmu_write(CPUMBState *env, uint32_t ext, uint32_t rn, uint32_t v)
{
    mmu_write(env, ext, rn, v);
}

void mb_cpu_transaction_failed(CPUState *cs, hwaddr physaddr, vaddr addr,
                               unsigned size, MMUAccessType access_type,
                               int mmu_idx, MemTxAttrs attrs,
                               MemTxResult response, uintptr_t retaddr)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUMBState *env = &cpu->env;

    qemu_log_mask(CPU_LOG_INT, "Transaction failed: vaddr 0x%" VADDR_PRIx
                  " physaddr 0x" TARGET_FMT_plx " size %d access type %s\n",
                  addr, physaddr, size,
                  access_type == MMU_INST_FETCH ? "INST_FETCH" :
                  (access_type == MMU_DATA_LOAD ? "DATA_LOAD" : "DATA_STORE"));

    assert(env->msr & MSR_EE);
    env->ear = addr;
    cs->exception_index = EXCP_HW_EXCP;
    cpu_loop_exit_restore(cs, retaddr);
}
#endif
