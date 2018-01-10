/*
 * RISC-V Emulation Helpers for QEMU.
 *
 * Author: Sagar Karandikar, sagark@eecs.berkeley.edu
 *
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
#include "qemu/log.h"
#include "cpu.h"
#include "qemu/main-loop.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"

#ifndef CONFIG_USER_ONLY

#if defined(TARGET_RISCV32)
static const char valid_vm_1_09[16] = {
    [VM_1_09_MBARE] = 1,
    [VM_1_09_SV32] = 1,
};
static const char valid_vm_1_10[16] = {
    [VM_1_10_MBARE] = 1,
    [VM_1_10_SV32] = 1
};
#elif defined(TARGET_RISCV64)
static const char valid_vm_1_09[16] = {
    [VM_1_09_MBARE] = 1,
    [VM_1_09_SV39] = 1,
    [VM_1_09_SV48] = 1,
};
static const char valid_vm_1_10[16] = {
    [VM_1_10_MBARE] = 1,
    [VM_1_10_SV39] = 1,
    [VM_1_10_SV48] = 1,
    [VM_1_10_SV57] = 1
};
#endif

static int validate_vm(CPURISCVState *env, target_ulong vm)
{
    return (env->priv_ver >= PRIV_VERSION_1_10_0) ?
        valid_vm_1_10[vm & 0xf] : valid_vm_1_09[vm & 0xf];
}

#endif

/* Exceptions processing helpers */
inline void QEMU_NORETURN do_raise_exception_err(CPURISCVState *env,
                                          uint32_t exception, uintptr_t pc)
{
    CPUState *cs = CPU(riscv_env_get_cpu(env));
    qemu_log_mask(CPU_LOG_INT, "%s: %d\n", __func__, exception);
    cs->exception_index = exception;
    cpu_loop_exit_restore(cs, pc);
}

void helper_raise_exception(CPURISCVState *env, uint32_t exception)
{
    do_raise_exception_err(env, exception, 0);
}

void helper_raise_exception_debug(CPURISCVState *env)
{
    do_raise_exception_err(env, EXCP_DEBUG, 0);
}

void helper_raise_exception_mbadaddr(CPURISCVState *env, uint32_t exception,
        target_ulong bad_pc) {
    env->badaddr = bad_pc;
    do_raise_exception_err(env, exception, 0);
}

/*
 * Handle writes to CSRs and any resulting special behavior
 *
 * Adapted from Spike's processor_t::set_csr
 */
inline void csr_write_helper(CPURISCVState *env, target_ulong val_to_write,
        target_ulong csrno)
{
    #ifdef RISCV_DEBUG_PRINT
    qemu_log_mask(LOG_TRACE, "Write CSR reg: 0x" TARGET_FMT_lx, csrno);
    qemu_log_mask(LOG_TRACE, "Write CSR val: 0x" TARGET_FMT_lx, val_to_write);
    #endif

#ifndef CONFIG_USER_ONLY
    uint64_t delegable_ints = MIP_SSIP | MIP_STIP | MIP_SEIP | (1 << IRQ_X_COP);
    uint64_t all_ints = delegable_ints | MIP_MSIP | MIP_MTIP;
#endif

    switch (csrno) {
    case CSR_FFLAGS:
        if (riscv_mstatus_fs(env)) {
            env->fflags = val_to_write & (FSR_AEXC >> FSR_AEXC_SHIFT);
        } else {
            helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        }
        break;
    case CSR_FRM:
        if (riscv_mstatus_fs(env)) {
            env->frm = val_to_write & (FSR_RD >> FSR_RD_SHIFT);
        } else {
            helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        }
        break;
    case CSR_FCSR:
        if (riscv_mstatus_fs(env)) {
            env->fflags = (val_to_write & FSR_AEXC) >> FSR_AEXC_SHIFT;
            env->frm = (val_to_write & FSR_RD) >> FSR_RD_SHIFT;
        } else {
            helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        }
        break;
#ifndef CONFIG_USER_ONLY
    case CSR_MSTATUS: {
        target_ulong mstatus = env->mstatus;
        target_ulong mask = 0;
        if (env->priv_ver <= PRIV_VERSION_1_09_1) {
            if ((val_to_write ^ mstatus) & (MSTATUS_MXR | MSTATUS_MPP |
                    MSTATUS_MPRV | MSTATUS_SUM | MSTATUS_VM)) {
                helper_tlb_flush(env);
            }
            mask = MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_MIE | MSTATUS_MPIE |
                MSTATUS_SPP | MSTATUS_FS | MSTATUS_MPRV | MSTATUS_SUM |
                MSTATUS_MPP | MSTATUS_MXR |
                (validate_vm(env, get_field(val_to_write, MSTATUS_VM)) ?
                    MSTATUS_VM : 0);
        }
        if (env->priv_ver >= PRIV_VERSION_1_10_0) {
            if ((val_to_write ^ mstatus) & (MSTATUS_MXR | MSTATUS_MPP |
                    MSTATUS_MPRV | MSTATUS_SUM)) {
                helper_tlb_flush(env);
            }
            mask = MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_MIE | MSTATUS_MPIE |
                MSTATUS_SPP | MSTATUS_FS | MSTATUS_MPRV | MSTATUS_SUM |
                MSTATUS_MPP | MSTATUS_MXR;
        }
        mstatus = (mstatus & ~mask) | (val_to_write & mask);
        int dirty = (mstatus & MSTATUS_FS) == MSTATUS_FS;
        dirty |= (mstatus & MSTATUS_XS) == MSTATUS_XS;
        mstatus = set_field(mstatus, MSTATUS_SD, dirty);
        env->mstatus = mstatus;
        break;
    }
    case CSR_MIP: {
        target_ulong mask = MIP_SSIP | MIP_STIP | MIP_SEIP;
        env->mip = (env->mip & ~mask) |
            (val_to_write & mask);
        qemu_mutex_lock_iothread();
        if (env->mip & MIP_SSIP) {
            qemu_irq_raise(SSIP_IRQ);
        } else {
            qemu_irq_lower(SSIP_IRQ);
        }
        if (env->mip & MIP_STIP) {
            qemu_irq_raise(STIP_IRQ);
        } else {
            qemu_irq_lower(STIP_IRQ);
        }
        if (env->mip & MIP_SEIP) {
            qemu_irq_raise(SEIP_IRQ);
        } else {
            qemu_irq_lower(SEIP_IRQ);
        }
        qemu_mutex_unlock_iothread();
        break;
    }
    case CSR_MIE: {
        env->mie = (env->mie & ~all_ints) |
            (val_to_write & all_ints);
        break;
    }
    case CSR_MIDELEG:
        env->mideleg = (env->mideleg & ~delegable_ints)
                                | (val_to_write & delegable_ints);
        break;
    case CSR_MEDELEG: {
        target_ulong mask = 0;
        mask |= 1ULL << (RISCV_EXCP_INST_ADDR_MIS);
        mask |= 1ULL << (RISCV_EXCP_INST_ACCESS_FAULT);
        mask |= 1ULL << (RISCV_EXCP_ILLEGAL_INST);
        mask |= 1ULL << (RISCV_EXCP_BREAKPOINT);
        mask |= 1ULL << (RISCV_EXCP_LOAD_ADDR_MIS);
        mask |= 1ULL << (RISCV_EXCP_LOAD_ACCESS_FAULT);
        mask |= 1ULL << (RISCV_EXCP_STORE_AMO_ADDR_MIS);
        mask |= 1ULL << (RISCV_EXCP_STORE_AMO_ACCESS_FAULT);
        mask |= 1ULL << (RISCV_EXCP_U_ECALL);
        mask |= 1ULL << (RISCV_EXCP_S_ECALL);
        mask |= 1ULL << (RISCV_EXCP_H_ECALL);
        mask |= 1ULL << (RISCV_EXCP_M_ECALL);
        mask |= 1ULL << (RISCV_EXCP_INST_PAGE_FAULT);
        mask |= 1ULL << (RISCV_EXCP_LOAD_PAGE_FAULT);
        mask |= 1ULL << (RISCV_EXCP_STORE_PAGE_FAULT);
        env->medeleg = (env->medeleg & ~mask)
                                | (val_to_write & mask);
        break;
    }
    case CSR_MINSTRET:
        qemu_log_mask(LOG_UNIMP, "CSR_MINSTRET: write not implemented");
        helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        break;
    case CSR_MCYCLE:
        qemu_log_mask(LOG_UNIMP, "CSR_MCYCLE: write not implemented");
        helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        break;
    case CSR_MINSTRETH:
        qemu_log_mask(LOG_UNIMP, "CSR_MINSTRETH: write not implemented");
        helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        break;
    case CSR_MCYCLEH:
        qemu_log_mask(LOG_UNIMP, "CSR_MCYCLEH: write not implemented");
        helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        break;
    case CSR_MUCOUNTEREN:
        env->mucounteren = val_to_write;
        break;
    case CSR_MSCOUNTEREN:
        env->mscounteren = val_to_write;
        break;
    case CSR_SSTATUS: {
        target_ulong ms = env->mstatus;
        target_ulong mask = SSTATUS_SIE | SSTATUS_SPIE | SSTATUS_UIE
            | SSTATUS_UPIE | SSTATUS_SPP | SSTATUS_FS | SSTATUS_XS
            | SSTATUS_SUM | SSTATUS_MXR | SSTATUS_SD;
        ms = (ms & ~mask) | (val_to_write & mask);
        csr_write_helper(env, ms, CSR_MSTATUS);
        break;
    }
    case CSR_SIP: {
        target_ulong next_mip = (env->mip & ~env->mideleg)
                                | (val_to_write & env->mideleg);
        csr_write_helper(env, next_mip, CSR_MIP);
        break;
    }
    case CSR_SIE: {
        target_ulong next_mie = (env->mie & ~env->mideleg)
                                | (val_to_write & env->mideleg);
        csr_write_helper(env, next_mie, CSR_MIE);
        break;
    }
    case CSR_SATP: /* CSR_SPTBR */ {
        if (env->priv_ver <= PRIV_VERSION_1_09_1 && (val_to_write ^ env->sptbr))
        {
            helper_tlb_flush(env);
            env->sptbr = val_to_write & (((target_ulong)
                1 << (TARGET_PHYS_ADDR_SPACE_BITS - PGSHIFT)) - 1);
        }
        if (env->priv_ver >= PRIV_VERSION_1_10_0 &&
            validate_vm(env, get_field(val_to_write, SATP_MODE)) &&
            ((val_to_write ^ env->satp) & (SATP_MODE | SATP_ASID | SATP_PPN)))
        {
            helper_tlb_flush(env);
            env->satp = val_to_write;
        }
        break;
    }
    case CSR_SEPC:
        env->sepc = val_to_write;
        break;
    case CSR_STVEC:
        if (val_to_write & 1) {
            qemu_log_mask(LOG_UNIMP, "CSR_STVEC: vectored traps not supported");
            helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        } else {
            env->stvec = val_to_write >> 2 << 2;
        }
        break;
    case CSR_SCOUNTEREN:
        env->scounteren = val_to_write;
        break;
    case CSR_SSCRATCH:
        env->sscratch = val_to_write;
        break;
    case CSR_SCAUSE:
        env->scause = val_to_write;
        break;
    case CSR_SBADADDR:
        env->sbadaddr = val_to_write;
        break;
    case CSR_MEPC:
        env->mepc = val_to_write;
        break;
    case CSR_MTVEC:
        if (val_to_write & 1) {
            qemu_log_mask(LOG_UNIMP, "CSR_MTVEC: vectored traps not supported");
            helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        } else {
            env->mtvec = val_to_write >> 2 << 2;
        }
        break;
    case CSR_MCOUNTEREN:
        env->mcounteren = val_to_write;
        break;
    case CSR_MSCRATCH:
        env->mscratch = val_to_write;
        break;
    case CSR_MCAUSE:
        env->mcause = val_to_write;
        break;
    case CSR_MBADADDR:
        env->mbadaddr = val_to_write;
        break;
    case CSR_MISA: {
        if (!(val_to_write & (1L << ('F' - 'A')))) {
            val_to_write &= ~(1L << ('D' - 'A'));
        }

        /* allow MAFDC bits in MISA to be modified */
        target_ulong mask = 0;
        mask |= 1L << ('M' - 'A');
        mask |= 1L << ('A' - 'A');
        mask |= 1L << ('F' - 'A');
        mask |= 1L << ('D' - 'A');
        mask |= 1L << ('C' - 'A');
        mask &= env->misa_mask;

        env->misa = (val_to_write & mask) | (env->misa & ~mask);
        break;
    }
    case CSR_PMPCFG0:
    case CSR_PMPCFG1:
    case CSR_PMPCFG2:
    case CSR_PMPCFG3:
       pmpcfg_csr_write(env, csrno - CSR_PMPCFG0, val_to_write);
       break;
    case CSR_PMPADDR0:
    case CSR_PMPADDR1:
    case CSR_PMPADDR2:
    case CSR_PMPADDR3:
    case CSR_PMPADDR4:
    case CSR_PMPADDR5:
    case CSR_PMPADDR6:
    case CSR_PMPADDR7:
    case CSR_PMPADDR8:
    case CSR_PMPADDR9:
    case CSR_PMPADDR10:
    case CSR_PMPADDR11:
    case CSR_PMPADDR12:
    case CSR_PMPADDR13:
    case CSR_PMPADDR14:
    case CSR_PMPADDR15:
       pmpaddr_csr_write(env, csrno - CSR_PMPADDR0, val_to_write);
       break;
#endif
    default:
        helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
    }
}

/*
 * Handle reads to CSRs and any resulting special behavior
 *
 * Adapted from Spike's processor_t::get_csr
 */
inline target_ulong csr_read_helper(CPURISCVState *env, target_ulong csrno)
{
    #ifdef RISCV_DEBUG_PRINT
    qemu_log_mask(LOG_TRACE, "Read CSR reg: 0x" TARGET_FMT_lx, csrno);
    #endif

#ifndef CONFIG_USER_ONLY
    target_ulong ctr_en = env->priv == PRV_U ? env->mucounteren :
                   env->priv == PRV_S ? env->mscounteren : -1U;
#else
    target_ulong ctr_en = env->mucounteren;
#endif
    target_ulong ctr_ok = (ctr_en >> (csrno & 31)) & 1;

    if (ctr_ok) {
        if (csrno >= CSR_HPMCOUNTER3 && csrno <= CSR_HPMCOUNTER31) {
            return 0;
        }
#if defined(TARGET_RISCV32)
        if (csrno >= CSR_HPMCOUNTER3H && csrno <= CSR_HPMCOUNTER31H) {
            return 0;
        }
#endif
    }
    if (csrno >= CSR_MHPMCOUNTER3 && csrno <= CSR_MHPMCOUNTER31) {
        return 0;
    }
#if defined(TARGET_RISCV32)
    if (csrno >= CSR_MHPMCOUNTER3 && csrno <= CSR_MHPMCOUNTER31) {
        return 0;
    }
#endif
    if (csrno >= CSR_MHPMEVENT3 && csrno <= CSR_MHPMEVENT31) {
        return 0;
    }

    switch (csrno) {
    case CSR_FFLAGS:
        if (riscv_mstatus_fs(env)) {
            return env->fflags;
        } else {
            helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        }
    case CSR_FRM:
        if (riscv_mstatus_fs(env)) {
            return env->frm;
        } else {
            helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        }
    case CSR_FCSR:
        if (riscv_mstatus_fs(env)) {
            return env->fflags << FSR_AEXC_SHIFT | env->frm << FSR_RD_SHIFT;
        } else {
            helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        }
#ifdef CONFIG_USER_ONLY
    case CSR_TIME:
    case CSR_CYCLE:
    case CSR_INSTRET:
        return (target_ulong)cpu_get_host_ticks();
    case CSR_TIMEH:
    case CSR_CYCLEH:
    case CSR_INSTRETH:
#if defined(TARGET_RISCV32)
        return (target_ulong)(cpu_get_host_ticks() >> 32);
#endif
        break;
#endif
#ifndef CONFIG_USER_ONLY
    case CSR_TIME:
        return cpu_riscv_read_rtc();
    case CSR_TIMEH:
        return (target_ulong)(cpu_riscv_read_rtc() >> 32);
    case CSR_INSTRET:
    case CSR_CYCLE:
        if (ctr_ok) {
            return cpu_riscv_read_instret(env);
        }
        break;
    case CSR_MINSTRET:
    case CSR_MCYCLE:
        return cpu_riscv_read_instret(env);
    case CSR_MINSTRETH:
    case CSR_MCYCLEH:
#if defined(TARGET_RISCV32)
        return cpu_riscv_read_instret(env) >> 32;
#endif
        break;
    case CSR_MUCOUNTEREN:
        return env->mucounteren;
    case CSR_MSCOUNTEREN:
        return env->mscounteren;
    case CSR_SSTATUS: {
        target_ulong mask = SSTATUS_SIE | SSTATUS_SPIE | SSTATUS_UIE
            | SSTATUS_UPIE | SSTATUS_SPP | SSTATUS_FS | SSTATUS_XS
            | SSTATUS_SUM |  SSTATUS_SD;
        if (env->priv_ver >= PRIV_VERSION_1_10_0) {
            mask |= SSTATUS_MXR;
        }
        return env->mstatus & mask;
    }
    case CSR_SIP:
        return env->mip & env->mideleg;
    case CSR_SIE:
        return env->mie & env->mideleg;
    case CSR_SEPC:
        return env->sepc;
    case CSR_SBADADDR:
        return env->sbadaddr;
    case CSR_STVEC:
        return env->stvec;
    case CSR_SCOUNTEREN:
        return env->scounteren;
    case CSR_SCAUSE:
        return env->scause;
    case CSR_SPTBR:
        if (env->priv_ver >= PRIV_VERSION_1_10_0) {
            return env->satp;
        } else {
            return env->sptbr;
        }
    case CSR_SSCRATCH:
        return env->sscratch;
    case CSR_MSTATUS:
        return env->mstatus;
    case CSR_MIP:
        return env->mip;
    case CSR_MIE:
        return env->mie;
    case CSR_MEPC:
        return env->mepc;
    case CSR_MSCRATCH:
        return env->mscratch;
    case CSR_MCAUSE:
        return env->mcause;
    case CSR_MBADADDR:
        return env->mbadaddr;
    case CSR_MISA:
        return env->misa;
    case CSR_MARCHID:
        return 0; /* as spike does */
    case CSR_MIMPID:
        return 0; /* as spike does */
    case CSR_MVENDORID:
        return 0; /* as spike does */
    case CSR_MHARTID:
        return env->mhartid;
    case CSR_MTVEC:
        return env->mtvec;
    case CSR_MCOUNTEREN:
        return env->mcounteren;
    case CSR_MEDELEG:
        return env->medeleg;
    case CSR_MIDELEG:
        return env->mideleg;
    case CSR_PMPCFG0:
    case CSR_PMPCFG1:
    case CSR_PMPCFG2:
    case CSR_PMPCFG3:
       return pmpcfg_csr_read(env, csrno - CSR_PMPCFG0);
    case CSR_PMPADDR0:
    case CSR_PMPADDR1:
    case CSR_PMPADDR2:
    case CSR_PMPADDR3:
    case CSR_PMPADDR4:
    case CSR_PMPADDR5:
    case CSR_PMPADDR6:
    case CSR_PMPADDR7:
    case CSR_PMPADDR8:
    case CSR_PMPADDR9:
    case CSR_PMPADDR10:
    case CSR_PMPADDR11:
    case CSR_PMPADDR12:
    case CSR_PMPADDR13:
    case CSR_PMPADDR14:
    case CSR_PMPADDR15:
       return pmpaddr_csr_read(env, csrno - CSR_PMPADDR0);
#endif
    }
    /* used by e.g. MTIME read */
    helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
    return 0;
}

/*
 * Check that CSR access is allowed.
 *
 * Adapted from Spike's decode.h:validate_csr
 */
void validate_csr(CPURISCVState *env, uint64_t which, uint64_t write)
{
#ifndef CONFIG_USER_ONLY
    unsigned csr_priv = get_field((which), 0x300);
    unsigned csr_read_only = get_field((which), 0xC00) == 3;
    if (((write) && csr_read_only) || (env->priv < csr_priv)) {
        do_raise_exception_err(env, RISCV_EXCP_ILLEGAL_INST, env->pc);
    }
#endif
}

target_ulong helper_csrrw(CPURISCVState *env, target_ulong src,
        target_ulong csr)
{
    validate_csr(env, csr, 1);
    uint64_t csr_backup = csr_read_helper(env, csr);
    csr_write_helper(env, src, csr);
    return csr_backup;
}

target_ulong helper_csrrs(CPURISCVState *env, target_ulong src,
        target_ulong csr, target_ulong rs1_pass)
{
    validate_csr(env, csr, rs1_pass != 0);
    uint64_t csr_backup = csr_read_helper(env, csr);
    if (rs1_pass != 0) {
        csr_write_helper(env, src | csr_backup, csr);
    }
    return csr_backup;
}

target_ulong helper_csrrc(CPURISCVState *env, target_ulong src,
        target_ulong csr, target_ulong rs1_pass)
{
    validate_csr(env, csr, rs1_pass != 0);
    uint64_t csr_backup = csr_read_helper(env, csr);
    if (rs1_pass != 0) {
        csr_write_helper(env, (~src) & csr_backup, csr);
    }
    return csr_backup;
}

#ifndef CONFIG_USER_ONLY

void riscv_set_mode(CPURISCVState *env, target_ulong newpriv)
{
    if (newpriv > PRV_M) {
        g_assert_not_reached();
    }
    if (newpriv == PRV_H) {
        newpriv = PRV_U;
    }
    helper_tlb_flush(env);
    env->priv = newpriv;
}

target_ulong helper_sret(CPURISCVState *env, target_ulong cpu_pc_deb)
{
    if (!(env->priv >= PRV_S)) {
        helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
    }

    target_ulong retpc = env->sepc;
    if (!riscv_has_ext(env, RVC) && (retpc & 0x3)) {
        helper_raise_exception(env, RISCV_EXCP_INST_ADDR_MIS);
    }

    target_ulong mstatus = env->mstatus;
    target_ulong prev_priv = get_field(mstatus, MSTATUS_SPP);
    mstatus = set_field(mstatus, MSTATUS_UIE << prev_priv,
                        get_field(mstatus, MSTATUS_SPIE));
    mstatus = set_field(mstatus, MSTATUS_SPIE, 0);
    mstatus = set_field(mstatus, MSTATUS_SPP, PRV_U);
    riscv_set_mode(env, prev_priv);
    csr_write_helper(env, mstatus, CSR_MSTATUS);

    return retpc;
}

target_ulong helper_mret(CPURISCVState *env, target_ulong cpu_pc_deb)
{
    if (!(env->priv >= PRV_M)) {
        helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
    }

    target_ulong retpc = env->mepc;
    if (!riscv_has_ext(env, RVC) && (retpc & 0x3)) {
        helper_raise_exception(env, RISCV_EXCP_INST_ADDR_MIS);
    }

    target_ulong mstatus = env->mstatus;
    target_ulong prev_priv = get_field(mstatus, MSTATUS_MPP);
    mstatus = set_field(mstatus, MSTATUS_UIE << prev_priv,
                        get_field(mstatus, MSTATUS_MPIE));
    mstatus = set_field(mstatus, MSTATUS_MPIE, 0);
    mstatus = set_field(mstatus, MSTATUS_MPP, PRV_U);
    riscv_set_mode(env, prev_priv);
    csr_write_helper(env, mstatus, CSR_MSTATUS);

    return retpc;
}


void helper_wfi(CPURISCVState *env)
{
    CPUState *cs = CPU(riscv_env_get_cpu(env));

    cs->halted = 1;
    cs->exception_index = EXCP_HLT;
    cpu_loop_exit(cs);
}

void helper_fence_i(CPURISCVState *env)
{
    /* FENCE.I is a no-op in qemu as self modifying code is detected */
}

void helper_tlb_flush(CPURISCVState *env)
{
    RISCVCPU *cpu = riscv_env_get_cpu(env);
    CPUState *cs = CPU(cpu);
    tlb_flush(cs);
}

#endif /* !CONFIG_USER_ONLY */
