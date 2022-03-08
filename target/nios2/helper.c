/*
 * Altera Nios II helper routines.
 *
 * Copyright (c) 2012 Chris Wulff <crwulff@gmail.com>
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
#include "qemu/host-utils.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/log.h"
#include "exec/helper-proto.h"
#include "semihosting/semihost.h"

#if defined(CONFIG_USER_ONLY)

void nios2_cpu_do_interrupt(CPUState *cs)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;
    cs->exception_index = -1;
    env->regs[R_EA] = env->pc + 4;
}

void nios2_cpu_record_sigsegv(CPUState *cs, vaddr addr,
                              MMUAccessType access_type,
                              bool maperr, uintptr_t retaddr)
{
    /* FIXME: Disentangle kuser page from linux-user sigsegv handling. */
    cs->exception_index = 0xaa;
    cpu_loop_exit_restore(cs, retaddr);
}

#else /* !CONFIG_USER_ONLY */

static void eic_do_interrupt(Nios2CPU *cpu)
{
    CPUNios2State *env = &cpu->env;
    uint32_t old_status = env->status;
    uint32_t old_rs = FIELD_EX32(old_status, CR_STATUS, CRS);
    uint32_t new_rs = cpu->rrs;

    env->status = FIELD_DP32(env->status, CR_STATUS, CRS, new_rs);
    env->status = FIELD_DP32(env->status, CR_STATUS, IL, cpu->ril);
    env->status = FIELD_DP32(env->status, CR_STATUS, NMI, cpu->rnmi);
    env->status &= ~(CR_STATUS_RSIE | CR_STATUS_U);
    env->status |= CR_STATUS_IH;
    nios2_update_crs(env);

    if (!(env->status & CR_STATUS_EH)) {
        env->status = FIELD_DP32(env->status, CR_STATUS, PRS, old_rs);
        if (new_rs == 0) {
            env->estatus = old_status;
        } else {
            if (new_rs != old_rs) {
                old_status |= CR_STATUS_SRS;
            }
            env->crs[R_SSTATUS] = old_status;
        }
        env->crs[R_EA] = env->pc + 4;
    }

    env->pc = cpu->rha;
}

void nios2_cpu_do_interrupt(CPUState *cs)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;
    uint32_t exception_addr = cpu->exception_addr;
    unsigned r_ea = R_EA;
    unsigned cr_estatus = CR_ESTATUS;

    switch (cs->exception_index) {
    case EXCP_IRQ:
        qemu_log_mask(CPU_LOG_INT, "interrupt at pc=%x\n", env->pc);
        if (cpu->eic_present) {
            eic_do_interrupt(cpu);
            return;
        }
        break;

    case EXCP_TLBD:
        if (env->status & CR_STATUS_EH) {
            qemu_log_mask(CPU_LOG_INT, "TLB MISS (double) at pc=%x\n", env->pc);
            /* Double TLB miss */
            env->tlbmisc |= CR_TLBMISC_DBL;
        } else {
            qemu_log_mask(CPU_LOG_INT, "TLB MISS (fast) at pc=%x\n", env->pc);
            env->tlbmisc &= ~CR_TLBMISC_DBL;
            env->tlbmisc |= CR_TLBMISC_WR;
            exception_addr = cpu->fast_tlb_miss_addr;
        }
        break;

    case EXCP_TLBR:
    case EXCP_TLBW:
    case EXCP_TLBX:
        qemu_log_mask(CPU_LOG_INT, "TLB PERM at pc=%x\n", env->pc);
        env->tlbmisc |= CR_TLBMISC_WR;
        break;

    case EXCP_SUPERA:
    case EXCP_SUPERI:
    case EXCP_SUPERD:
        qemu_log_mask(CPU_LOG_INT, "SUPERVISOR exception at pc=%x\n", env->pc);
        break;

    case EXCP_ILLEGAL:
    case EXCP_TRAP:
        qemu_log_mask(CPU_LOG_INT, "TRAP exception at pc=%x\n", env->pc);
        break;

    case EXCP_SEMIHOST:
        qemu_log_mask(CPU_LOG_INT, "BREAK semihosting at pc=%x\n", env->pc);
        env->pc += 4;
        do_nios2_semihosting(env);
        return;

    case EXCP_BREAK:
        qemu_log_mask(CPU_LOG_INT, "BREAK exception at pc=%x\n", env->pc);
        r_ea = R_BA;
        cr_estatus = CR_BSTATUS;
        break;

    default:
        cpu_abort(cs, "unhandled exception type=%d\n", cs->exception_index);
    }

    /*
     * Finish Internal Interrupt or Noninterrupt Exception.
     */

    env->status &= ~R_CR_STATUS_CRS_MASK;
    nios2_update_crs(env);

    if (!(env->status & CR_STATUS_EH)) {
        env->ctrl[cr_estatus] = env->status;
        env->crs[r_ea] = env->pc + 4;
        env->status |= CR_STATUS_EH;
    }
    env->status &= ~(CR_STATUS_PIE | CR_STATUS_U);

    env->pc = exception_addr;
    env->exception = FIELD_DP32(env->exception, CR_EXCEPTION, CAUSE,
                                cs->exception_index);
}

hwaddr nios2_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;
    target_ulong vaddr, paddr = 0;
    Nios2MMULookup lu;
    unsigned int hit;

    if (cpu->mmu_present && (addr < 0xC0000000)) {
        hit = mmu_translate(env, &lu, addr, 0, 0);
        if (hit) {
            vaddr = addr & TARGET_PAGE_MASK;
            paddr = lu.paddr + vaddr - lu.vaddr;
        } else {
            paddr = -1;
            qemu_log("cpu_get_phys_page debug MISS: %#" PRIx64 "\n", addr);
        }
    } else {
        paddr = addr & TARGET_PAGE_MASK;
    }

    return paddr;
}

void nios2_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                   MMUAccessType access_type,
                                   int mmu_idx, uintptr_t retaddr)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;

    env->badaddr = addr;
    env->exception = EXCP_UNALIGN << 2;
    helper_raise_exception(env, EXCP_UNALIGN);
}

bool nios2_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                        MMUAccessType access_type, int mmu_idx,
                        bool probe, uintptr_t retaddr)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;
    unsigned int excp = EXCP_TLBD;
    target_ulong vaddr, paddr;
    Nios2MMULookup lu;
    unsigned int hit;

    if (!cpu->mmu_present) {
        /* No MMU */
        address &= TARGET_PAGE_MASK;
        tlb_set_page(cs, address, address, PAGE_BITS,
                     mmu_idx, TARGET_PAGE_SIZE);
        return true;
    }

    if (MMU_SUPERVISOR_IDX == mmu_idx) {
        if (address >= 0xC0000000) {
            /* Kernel physical page - TLB bypassed */
            address &= TARGET_PAGE_MASK;
            tlb_set_page(cs, address, address, PAGE_BITS,
                         mmu_idx, TARGET_PAGE_SIZE);
            return true;
        }
    } else {
        if (address >= 0x80000000) {
            /* Illegal access from user mode */
            if (probe) {
                return false;
            }
            cs->exception_index = EXCP_SUPERA;
            env->badaddr = address;
            cpu_loop_exit_restore(cs, retaddr);
        }
    }

    /* Virtual page.  */
    hit = mmu_translate(env, &lu, address, access_type, mmu_idx);
    if (hit) {
        vaddr = address & TARGET_PAGE_MASK;
        paddr = lu.paddr + vaddr - lu.vaddr;

        if (((access_type == MMU_DATA_LOAD) && (lu.prot & PAGE_READ)) ||
            ((access_type == MMU_DATA_STORE) && (lu.prot & PAGE_WRITE)) ||
            ((access_type == MMU_INST_FETCH) && (lu.prot & PAGE_EXEC))) {
            tlb_set_page(cs, vaddr, paddr, lu.prot,
                         mmu_idx, TARGET_PAGE_SIZE);
            return true;
        }

        /* Permission violation */
        excp = (access_type == MMU_DATA_LOAD ? EXCP_TLBR :
                access_type == MMU_DATA_STORE ? EXCP_TLBW : EXCP_TLBX);
    }

    if (probe) {
        return false;
    }

    env->tlbmisc = FIELD_DP32(env->tlbmisc, CR_TLBMISC, D,
                              access_type == MMU_INST_FETCH);
    env->pteaddr = FIELD_DP32(env->pteaddr, CR_PTEADDR, VPN,
                              address >> TARGET_PAGE_BITS);
    env->mmu.pteaddr_wr = env->pteaddr;

    cs->exception_index = excp;
    env->badaddr = address;
    cpu_loop_exit_restore(cs, retaddr);
}
#endif /* !CONFIG_USER_ONLY */
