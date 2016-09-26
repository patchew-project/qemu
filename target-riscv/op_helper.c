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
#include <stdlib.h>
#include "cpu.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"

int validate_priv(target_ulong priv)
{
    return priv == PRV_U || priv == PRV_S || priv == PRV_M;
}

void set_privilege(CPURISCVState *env, target_ulong newpriv)
{
    if (!validate_priv(newpriv)) {
        printf("INVALID PRIV SET\n");
        exit(1);
    }
    helper_tlb_flush(env);
    env->priv = newpriv;
}

/* Exceptions processing helpers */
static inline void QEMU_NORETURN do_raise_exception_err(CPURISCVState *env,
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

#if defined(TARGET_RISCV64)
target_ulong helper_mulhsu(CPURISCVState *env, target_ulong arg1,
                          target_ulong arg2)
{
    int64_t a = arg1;
    uint64_t b = arg2;
    return (int64_t)((__int128_t)a * b >> 64);
}
#endif

/*
 * Handle writes to CSRs and any resulting special behavior
 *
 * Adapted from Spike's processor_t::set_csr
 */
inline void csr_write_helper(CPURISCVState *env, target_ulong val_to_write,
        target_ulong csrno)
{
}

/*
 * Handle reads to CSRs and any resulting special behavior
 *
 * Adapted from Spike's processor_t::get_csr
 */
inline target_ulong csr_read_helper(CPURISCVState *env, target_ulong csrno)
{
    return 0;
}

/*
 * Check that CSR access is allowed.
 *
 * Adapted from Spike's decode.h:validate_csr
 */
void validate_csr(CPURISCVState *env, uint64_t which, uint64_t write,
        uint64_t new_pc) {
    unsigned csr_priv = get_field((which), 0x300);
    unsigned csr_read_only = get_field((which), 0xC00) == 3;
    if (((write) && csr_read_only) || (env->priv < csr_priv)) {
        do_raise_exception_err(env, RISCV_EXCP_ILLEGAL_INST, new_pc);
    }
    return;
}

target_ulong helper_csrrw(CPURISCVState *env, target_ulong src,
        target_ulong csr, target_ulong new_pc)
{
    validate_csr(env, csr, 1, new_pc);
    uint64_t csr_backup = csr_read_helper(env, csr);
    csr_write_helper(env, src, csr);
    return csr_backup;
}

target_ulong helper_csrrs(CPURISCVState *env, target_ulong src,
        target_ulong csr, target_ulong new_pc, target_ulong rs1_pass)
{
    validate_csr(env, csr, rs1_pass != 0, new_pc);
    uint64_t csr_backup = csr_read_helper(env, csr);
    if (rs1_pass != 0) {
        csr_write_helper(env, src | csr_backup, csr);
    }
    return csr_backup;
}

target_ulong helper_csrrc(CPURISCVState *env, target_ulong src,
        target_ulong csr, target_ulong new_pc, target_ulong rs1_pass) {
    validate_csr(env, csr, rs1_pass != 0, new_pc);
    uint64_t csr_backup = csr_read_helper(env, csr);
    if (rs1_pass != 0) {
        csr_write_helper(env, (~src) & csr_backup, csr);
    }
    return csr_backup;
}

target_ulong helper_sret(CPURISCVState *env, target_ulong cpu_pc_deb)
{
    if (!(env->priv >= PRV_S)) {
        helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
    }

    target_ulong retpc = env->csr[CSR_SEPC];
    if (retpc & 0x3) {
        helper_raise_exception(env, RISCV_EXCP_INST_ADDR_MIS);
    }

    target_ulong mstatus = env->csr[CSR_MSTATUS];
    target_ulong prev_priv = get_field(mstatus, MSTATUS_SPP);
    mstatus = set_field(mstatus, MSTATUS_UIE << prev_priv,
                        get_field(mstatus, MSTATUS_SPIE));
    mstatus = set_field(mstatus, MSTATUS_SPIE, 0);
    mstatus = set_field(mstatus, MSTATUS_SPP, PRV_U);
    set_privilege(env, prev_priv);
    csr_write_helper(env, mstatus, CSR_MSTATUS);

    return retpc;
}

target_ulong helper_mret(CPURISCVState *env, target_ulong cpu_pc_deb)
{
    if (!(env->priv >= PRV_M)) {
        helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
    }

    target_ulong retpc = env->csr[CSR_MEPC];
    if (retpc & 0x3) {
        helper_raise_exception(env, RISCV_EXCP_INST_ADDR_MIS);
    }

    target_ulong mstatus = env->csr[CSR_MSTATUS];
    target_ulong prev_priv = get_field(mstatus, MSTATUS_MPP);
    mstatus = set_field(mstatus, MSTATUS_UIE << prev_priv,
                        get_field(mstatus, MSTATUS_MPIE));
    mstatus = set_field(mstatus, MSTATUS_MPIE, 0);
    mstatus = set_field(mstatus, MSTATUS_MPP, PRV_U);
    set_privilege(env, prev_priv);
    csr_write_helper(env, mstatus, CSR_MSTATUS);

    return retpc;
}

#ifndef CONFIG_USER_ONLY

void helper_fence_i(CPURISCVState *env)
{
    RISCVCPU *cpu = riscv_env_get_cpu(env);
    CPUState *cs = CPU(cpu);
    /* Flush QEMU's TLB */
    tlb_flush(cs, 1);
    /* ARM port seems to not know if this is okay inside a TB
       But we need to do it */
    tb_flush(cs);
}

void helper_tlb_flush(CPURISCVState *env)
{
    RISCVCPU *cpu = riscv_env_get_cpu(env);
    tlb_flush(CPU(cpu), 1);
}

void riscv_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                   MMUAccessType access_type, int mmu_idx,
                                   uintptr_t retaddr)
{
}

void tlb_fill(CPUState *cs, target_ulong addr, MMUAccessType access_type,
        int mmu_idx, uintptr_t retaddr)
{
}

void riscv_cpu_unassigned_access(CPUState *cs, hwaddr addr, bool is_write,
        bool is_exec, int unused, unsigned size)
{
}

#endif /* !CONFIG_USER_ONLY */
