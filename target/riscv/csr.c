/*
 * RISC-V Control and Status Registers.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "qemu/main-loop.h"
#include "exec/exec-all.h"

/* CSR function table */
static riscv_csr_operations csr_ops[];

/* CSR function table constants */
enum {
    CSR_TABLE_SIZE = 0x1000
};

/* CSR function table public API */
void riscv_get_csr_ops(int csrno, riscv_csr_operations *ops)
{
    *ops = csr_ops[csrno & (CSR_TABLE_SIZE - 1)];
}

void riscv_set_csr_ops(int csrno, riscv_csr_operations *ops)
{
    csr_ops[csrno & (CSR_TABLE_SIZE - 1)] = *ops;
}

/* Predicates */
static int fs(CPURISCVState *env, int csrno)
{
#if !defined(CONFIG_USER_ONLY)
    /* loose check condition for fcsr in vector extension */
    if ((csrno == CSR_FCSR) && (env->misa & RVV)) {
        return 0;
    }
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
#endif
    return 0;
}

static int vs(CPURISCVState *env, int csrno)
{
    if (env->misa & RVV) {
        return 0;
    }
    return -1;
}

static int ctr(CPURISCVState *env, int csrno)
{
#if !defined(CONFIG_USER_ONLY)
    CPUState *cs = env_cpu(env);
    RISCVCPU *cpu = RISCV_CPU(cs);

    if (!cpu->cfg.ext_counters) {
        /* The Counters extensions is not enabled */
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    if (riscv_cpu_virt_enabled(env)) {
        switch (csrno) {
        case CSR_CYCLE:
            if (!get_field(env->hcounteren, HCOUNTEREN_CY) &&
                get_field(env->mcounteren, HCOUNTEREN_CY)) {
                return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
            }
            break;
        case CSR_TIME:
            if (!get_field(env->hcounteren, HCOUNTEREN_TM) &&
                get_field(env->mcounteren, HCOUNTEREN_TM)) {
                return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
            }
            break;
        case CSR_INSTRET:
            if (!get_field(env->hcounteren, HCOUNTEREN_IR) &&
                get_field(env->mcounteren, HCOUNTEREN_IR)) {
                return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
            }
            break;
        case CSR_HPMCOUNTER3...CSR_HPMCOUNTER31:
            if (!get_field(env->hcounteren, 1 << (csrno - CSR_HPMCOUNTER3)) &&
                get_field(env->mcounteren, 1 << (csrno - CSR_HPMCOUNTER3))) {
                return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
            }
            break;
        }
        if (riscv_cpu_is_32bit(env)) {
            switch (csrno) {
            case CSR_CYCLEH:
                if (!get_field(env->hcounteren, HCOUNTEREN_CY) &&
                    get_field(env->mcounteren, HCOUNTEREN_CY)) {
                    return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
                }
                break;
            case CSR_TIMEH:
                if (!get_field(env->hcounteren, HCOUNTEREN_TM) &&
                    get_field(env->mcounteren, HCOUNTEREN_TM)) {
                    return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
                }
                break;
            case CSR_INSTRETH:
                if (!get_field(env->hcounteren, HCOUNTEREN_IR) &&
                    get_field(env->mcounteren, HCOUNTEREN_IR)) {
                    return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
                }
                break;
            case CSR_HPMCOUNTER3H...CSR_HPMCOUNTER31H:
                if (!get_field(env->hcounteren, 1 << (csrno - CSR_HPMCOUNTER3H)) &&
                    get_field(env->mcounteren, 1 << (csrno - CSR_HPMCOUNTER3H))) {
                    return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
                }
                break;
            }
        }
    }
#endif
    return 0;
}

static int ctr32(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_is_32bit(env)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    return ctr(env, csrno);
}

#if !defined(CONFIG_USER_ONLY)
static int any(CPURISCVState *env, int csrno)
{
    return 0;
}

static int any32(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_is_32bit(env)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    return any(env, csrno);

}

static int smode(CPURISCVState *env, int csrno)
{
    return -!riscv_has_ext(env, RVS);
}

static int hmode(CPURISCVState *env, int csrno)
{
    if (riscv_has_ext(env, RVS) &&
        riscv_has_ext(env, RVH)) {
        /* Hypervisor extension is supported */
        if ((env->priv == PRV_S && !riscv_cpu_virt_enabled(env)) ||
            env->priv == PRV_M) {
            return 0;
        } else {
            return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
    }

    return -RISCV_EXCP_ILLEGAL_INST;
}

static int hmode32(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_is_32bit(env)) {
        return 0;
    }

    return hmode(env, csrno);

}

static int umode(CPURISCVState *env, int csrno)
{
    return -!riscv_has_ext(env, RVU);
}

static int pmp(CPURISCVState *env, int csrno)
{
    return -!riscv_feature(env, RISCV_FEATURE_PMP);
}
#endif

/* User Floating-Point CSRs */
static int read_fflags(CPURISCVState *env, int csrno, target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
#endif
    *val = riscv_cpu_get_fflags(env);
    return 0;
}

static int write_fflags(CPURISCVState *env, int csrno, target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    env->mstatus |= MSTATUS_FS;
#endif
    riscv_cpu_set_fflags(env, val & (FSR_AEXC >> FSR_AEXC_SHIFT));
    return 0;
}

static int read_frm(CPURISCVState *env, int csrno, target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
#endif
    *val = env->frm;
    return 0;
}

static int write_frm(CPURISCVState *env, int csrno, target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    env->mstatus |= MSTATUS_FS;
#endif
    env->frm = val & (FSR_RD >> FSR_RD_SHIFT);
    return 0;
}

static int read_fcsr(CPURISCVState *env, int csrno, target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
#endif
    *val = (riscv_cpu_get_fflags(env) << FSR_AEXC_SHIFT)
        | (env->frm << FSR_RD_SHIFT);
    if (vs(env, csrno) >= 0) {
        *val |= (env->vxrm << FSR_VXRM_SHIFT)
                | (env->vxsat << FSR_VXSAT_SHIFT);
    }
    return 0;
}

static int write_fcsr(CPURISCVState *env, int csrno, target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    env->mstatus |= MSTATUS_FS;
#endif
    env->frm = (val & FSR_RD) >> FSR_RD_SHIFT;
    if (vs(env, csrno) >= 0) {
        env->vxrm = (val & FSR_VXRM) >> FSR_VXRM_SHIFT;
        env->vxsat = (val & FSR_VXSAT) >> FSR_VXSAT_SHIFT;
    }
    riscv_cpu_set_fflags(env, (val & FSR_AEXC) >> FSR_AEXC_SHIFT);
    return 0;
}

static int read_vtype(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vtype;
    return 0;
}

static int read_vl(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vl;
    return 0;
}

static int read_vxrm(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vxrm;
    return 0;
}

static int write_vxrm(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vxrm = val;
    return 0;
}

static int read_vxsat(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vxsat;
    return 0;
}

static int write_vxsat(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vxsat = val;
    return 0;
}

static int read_vstart(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vstart;
    return 0;
}

static int write_vstart(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vstart = val;
    return 0;
}

/* User Timers and Counters */
static int read_instret(CPURISCVState *env, int csrno, target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (icount_enabled()) {
        *val = icount_get();
    } else {
        *val = cpu_get_host_ticks();
    }
#else
    *val = cpu_get_host_ticks();
#endif
    return 0;
}

static int read_instreth(CPURISCVState *env, int csrno, target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (icount_enabled()) {
        *val = icount_get() >> 32;
    } else {
        *val = cpu_get_host_ticks() >> 32;
    }
#else
    *val = cpu_get_host_ticks() >> 32;
#endif
    return 0;
}

#if defined(CONFIG_USER_ONLY)
static int read_time(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = cpu_get_host_ticks();
    return 0;
}

static int read_timeh(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = cpu_get_host_ticks() >> 32;
    return 0;
}

#else /* CONFIG_USER_ONLY */

static int read_time(CPURISCVState *env, int csrno, target_ulong *val)
{
    uint64_t delta = riscv_cpu_virt_enabled(env) ? env->htimedelta : 0;

    if (!env->rdtime_fn) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    *val = env->rdtime_fn(env->rdtime_fn_arg) + delta;
    return 0;
}

static int read_timeh(CPURISCVState *env, int csrno, target_ulong *val)
{
    uint64_t delta = riscv_cpu_virt_enabled(env) ? env->htimedelta : 0;

    if (!env->rdtime_fn) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    *val = (env->rdtime_fn(env->rdtime_fn_arg) + delta) >> 32;
    return 0;
}

/* Machine constants */

#define M_MODE_INTERRUPTS  (MIP_MSIP | MIP_MTIP | MIP_MEIP)
#define S_MODE_INTERRUPTS  (MIP_SSIP | MIP_STIP | MIP_SEIP)
#define VS_MODE_INTERRUPTS (MIP_VSSIP | MIP_VSTIP | MIP_VSEIP)

static const target_ulong delegable_ints = S_MODE_INTERRUPTS |
                                           VS_MODE_INTERRUPTS;
static const target_ulong all_ints = M_MODE_INTERRUPTS | S_MODE_INTERRUPTS |
                                     VS_MODE_INTERRUPTS;
static const target_ulong delegable_excps =
    (1ULL << (RISCV_EXCP_INST_ADDR_MIS)) |
    (1ULL << (RISCV_EXCP_INST_ACCESS_FAULT)) |
    (1ULL << (RISCV_EXCP_ILLEGAL_INST)) |
    (1ULL << (RISCV_EXCP_BREAKPOINT)) |
    (1ULL << (RISCV_EXCP_LOAD_ADDR_MIS)) |
    (1ULL << (RISCV_EXCP_LOAD_ACCESS_FAULT)) |
    (1ULL << (RISCV_EXCP_STORE_AMO_ADDR_MIS)) |
    (1ULL << (RISCV_EXCP_STORE_AMO_ACCESS_FAULT)) |
    (1ULL << (RISCV_EXCP_U_ECALL)) |
    (1ULL << (RISCV_EXCP_S_ECALL)) |
    (1ULL << (RISCV_EXCP_VS_ECALL)) |
    (1ULL << (RISCV_EXCP_M_ECALL)) |
    (1ULL << (RISCV_EXCP_INST_PAGE_FAULT)) |
    (1ULL << (RISCV_EXCP_LOAD_PAGE_FAULT)) |
    (1ULL << (RISCV_EXCP_STORE_PAGE_FAULT)) |
    (1ULL << (RISCV_EXCP_INST_GUEST_PAGE_FAULT)) |
    (1ULL << (RISCV_EXCP_LOAD_GUEST_ACCESS_FAULT)) |
    (1ULL << (RISCV_EXCP_VIRT_INSTRUCTION_FAULT)) |
    (1ULL << (RISCV_EXCP_STORE_GUEST_AMO_ACCESS_FAULT));
static const target_ulong sstatus_v1_10_mask = SSTATUS_SIE | SSTATUS_SPIE |
    SSTATUS_UIE | SSTATUS_UPIE | SSTATUS_SPP | SSTATUS_FS | SSTATUS_XS |
    SSTATUS_SUM | SSTATUS_MXR | SSTATUS_SD;
static const target_ulong sip_writable_mask = SIP_SSIP | MIP_USIP | MIP_UEIP;
static const target_ulong hip_writable_mask = MIP_VSSIP | MIP_VSTIP | MIP_VSEIP;
static const target_ulong vsip_writable_mask = MIP_VSSIP;

static const char valid_vm_1_10_32[16] = {
    [VM_1_10_MBARE] = 1,
    [VM_1_10_SV32] = 1
};

static const char valid_vm_1_10_64[16] = {
    [VM_1_10_MBARE] = 1,
    [VM_1_10_SV39] = 1,
    [VM_1_10_SV48] = 1,
    [VM_1_10_SV57] = 1
};

/* Machine Information Registers */
static int read_zero(CPURISCVState *env, int csrno, target_ulong *val)
{
    return *val = 0;
}

static int read_mhartid(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mhartid;
    return 0;
}

/* Machine Trap Setup */
static int read_mstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mstatus;
    return 0;
}

static int validate_vm(CPURISCVState *env, target_ulong vm)
{
    if (riscv_cpu_is_32bit(env)) {
        return valid_vm_1_10_32[vm & 0xf];
    } else {
        return valid_vm_1_10_64[vm & 0xf];
    }
}

static int write_mstatus(CPURISCVState *env, int csrno, target_ulong val)
{
    uint64_t mstatus = env->mstatus;
    uint64_t mask = 0;
    int dirty;

    /* flush tlb on mstatus fields that affect VM */
    if ((val ^ mstatus) & (MSTATUS_MXR | MSTATUS_MPP | MSTATUS_MPV |
            MSTATUS_MPRV | MSTATUS_SUM)) {
        tlb_flush(env_cpu(env));
    }
    mask = MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_MIE | MSTATUS_MPIE |
        MSTATUS_SPP | MSTATUS_FS | MSTATUS_MPRV | MSTATUS_SUM |
        MSTATUS_MPP | MSTATUS_MXR | MSTATUS_TVM | MSTATUS_TSR |
        MSTATUS_TW;

    if (!riscv_cpu_is_32bit(env)) {
        /*
         * RV32: MPV and GVA are not in mstatus. The current plan is to
         * add them to mstatush. For now, we just don't support it.
         */
        mask |= MSTATUS_MPV | MSTATUS_GVA;
    }

    mstatus = (mstatus & ~mask) | (val & mask);

    dirty = ((mstatus & MSTATUS_FS) == MSTATUS_FS) |
            ((mstatus & MSTATUS_XS) == MSTATUS_XS);
    mstatus = set_field(mstatus, MSTATUS_SD, dirty);
    env->mstatus = mstatus;

    return 0;
}

static int read_mstatush(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mstatus >> 32;
    return 0;
}

static int write_mstatush(CPURISCVState *env, int csrno, target_ulong val)
{
    uint64_t valh = (uint64_t)val << 32;
    uint64_t mask = MSTATUS_MPV | MSTATUS_GVA;

    if ((valh ^ env->mstatus) & (MSTATUS_MPV)) {
        tlb_flush(env_cpu(env));
    }

    env->mstatus = (env->mstatus & ~mask) | (valh & mask);

    return 0;
}

static int read_misa(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->misa;
    return 0;
}

static int write_misa(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!riscv_feature(env, RISCV_FEATURE_MISA)) {
        /* drop write to misa */
        return 0;
    }

    /* 'I' or 'E' must be present */
    if (!(val & (RVI | RVE))) {
        /* It is not, drop write to misa */
        return 0;
    }

    /* 'E' excludes all other extensions */
    if (val & RVE) {
        /* when we support 'E' we can do "val = RVE;" however
         * for now we just drop writes if 'E' is present.
         */
        return 0;
    }

    /* Mask extensions that are not supported by this hart */
    val &= env->misa_mask;

    /* Mask extensions that are not supported by QEMU */
    val &= (RVI | RVE | RVM | RVA | RVF | RVD | RVC | RVS | RVU);

    /* 'D' depends on 'F', so clear 'D' if 'F' is not present */
    if ((val & RVD) && !(val & RVF)) {
        val &= ~RVD;
    }

    /* Suppress 'C' if next instruction is not aligned
     * TODO: this should check next_pc
     */
    if ((val & RVC) && (GETPC() & ~3) != 0) {
        val &= ~RVC;
    }

    /* misa.MXL writes are not supported by QEMU */
    val = (env->misa & MISA_MXL) | (val & ~MISA_MXL);

    /* flush translation cache */
    if (val != env->misa) {
        tb_flush(env_cpu(env));
    }

    env->misa = val;

    return 0;
}

static int read_medeleg(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->medeleg;
    return 0;
}

static int write_medeleg(CPURISCVState *env, int csrno, target_ulong val)
{
    env->medeleg = (env->medeleg & ~delegable_excps) | (val & delegable_excps);
    return 0;
}

static int read_mideleg(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mideleg;
    return 0;
}

static int write_mideleg(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mideleg = (env->mideleg & ~delegable_ints) | (val & delegable_ints);
    if (riscv_has_ext(env, RVH)) {
        env->mideleg |= VS_MODE_INTERRUPTS;
    }
    return 0;
}

static int read_mie(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mie;
    return 0;
}

static int write_mie(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mie = (env->mie & ~all_ints) | (val & all_ints);
    return 0;
}

static int read_mtvec(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mtvec;
    return 0;
}

static int write_mtvec(CPURISCVState *env, int csrno, target_ulong val)
{
    /* bits [1:0] encode mode; 0 = direct, 1 = vectored, 2 >= reserved */
    if ((val & 3) < 2) {
        env->mtvec = val;
    } else {
        qemu_log_mask(LOG_UNIMP, "CSR_MTVEC: reserved mode not supported\n");
    }
    return 0;
}

static int read_mcounteren(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mcounteren;
    return 0;
}

static int write_mcounteren(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mcounteren = val;
    return 0;
}

/* This regiser is replaced with CSR_MCOUNTINHIBIT in 1.11.0 */
static int read_mscounteren(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (env->priv_ver < PRIV_VERSION_1_11_0) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    *val = env->mcounteren;
    return 0;
}

/* This regiser is replaced with CSR_MCOUNTINHIBIT in 1.11.0 */
static int write_mscounteren(CPURISCVState *env, int csrno, target_ulong val)
{
    if (env->priv_ver < PRIV_VERSION_1_11_0) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    env->mcounteren = val;
    return 0;
}

/* Machine Trap Handling */
static int read_mscratch(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mscratch;
    return 0;
}

static int write_mscratch(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mscratch = val;
    return 0;
}

static int read_mepc(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mepc;
    return 0;
}

static int write_mepc(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mepc = val;
    return 0;
}

static int read_mcause(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mcause;
    return 0;
}

static int write_mcause(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mcause = val;
    return 0;
}

static int read_mbadaddr(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mbadaddr;
    return 0;
}

static int write_mbadaddr(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mbadaddr = val;
    return 0;
}

static int rmw_mip(CPURISCVState *env, int csrno, target_ulong *ret_value,
                   target_ulong new_value, target_ulong write_mask)
{
    RISCVCPU *cpu = env_archcpu(env);
    /* Allow software control of delegable interrupts not claimed by hardware */
    target_ulong mask = write_mask & delegable_ints & ~env->miclaim;
    uint32_t old_mip;

    if (mask) {
        old_mip = riscv_cpu_update_mip(cpu, mask, (new_value & mask));
    } else {
        old_mip = env->mip;
    }

    if (ret_value) {
        *ret_value = old_mip;
    }

    return 0;
}

/* Supervisor Trap Setup */
static int read_sstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    target_ulong mask = (sstatus_v1_10_mask);
    *val = env->mstatus & mask;
    return 0;
}

static int write_sstatus(CPURISCVState *env, int csrno, target_ulong val)
{
    target_ulong mask = (sstatus_v1_10_mask);
    target_ulong newval = (env->mstatus & ~mask) | (val & mask);
    return write_mstatus(env, CSR_MSTATUS, newval);
}

static int read_sie(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (riscv_cpu_virt_enabled(env)) {
        /* Tell the guest the VS bits, shifted to the S bit locations */
        *val = (env->mie & env->mideleg & VS_MODE_INTERRUPTS) >> 1;
    } else {
        *val = env->mie & env->mideleg;
    }
    return 0;
}

static int write_sie(CPURISCVState *env, int csrno, target_ulong val)
{
    target_ulong newval;

    if (riscv_cpu_virt_enabled(env)) {
        /* Shift the guests S bits to VS */
        newval = (env->mie & ~VS_MODE_INTERRUPTS) |
                 ((val << 1) & VS_MODE_INTERRUPTS);
    } else {
        newval = (env->mie & ~S_MODE_INTERRUPTS) | (val & S_MODE_INTERRUPTS);
    }

    return write_mie(env, CSR_MIE, newval);
}

static int read_stvec(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->stvec;
    return 0;
}

static int write_stvec(CPURISCVState *env, int csrno, target_ulong val)
{
    /* bits [1:0] encode mode; 0 = direct, 1 = vectored, 2 >= reserved */
    if ((val & 3) < 2) {
        env->stvec = val;
    } else {
        qemu_log_mask(LOG_UNIMP, "CSR_STVEC: reserved mode not supported\n");
    }
    return 0;
}

static int read_scounteren(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->scounteren;
    return 0;
}

static int write_scounteren(CPURISCVState *env, int csrno, target_ulong val)
{
    env->scounteren = val;
    return 0;
}

/* Supervisor Trap Handling */
static int read_sscratch(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->sscratch;
    return 0;
}

static int write_sscratch(CPURISCVState *env, int csrno, target_ulong val)
{
    env->sscratch = val;
    return 0;
}

static int read_sepc(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->sepc;
    return 0;
}

static int write_sepc(CPURISCVState *env, int csrno, target_ulong val)
{
    env->sepc = val;
    return 0;
}

static int read_scause(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->scause;
    return 0;
}

static int write_scause(CPURISCVState *env, int csrno, target_ulong val)
{
    env->scause = val;
    return 0;
}

static int read_sbadaddr(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->sbadaddr;
    return 0;
}

static int write_sbadaddr(CPURISCVState *env, int csrno, target_ulong val)
{
    env->sbadaddr = val;
    return 0;
}

static int rmw_sip(CPURISCVState *env, int csrno, target_ulong *ret_value,
                   target_ulong new_value, target_ulong write_mask)
{
    int ret;

    if (riscv_cpu_virt_enabled(env)) {
        /* Shift the new values to line up with the VS bits */
        ret = rmw_mip(env, CSR_MSTATUS, ret_value, new_value << 1,
                      (write_mask & sip_writable_mask) << 1 & env->mideleg);
        ret &= vsip_writable_mask;
        ret >>= 1;
    } else {
        ret = rmw_mip(env, CSR_MSTATUS, ret_value, new_value,
                      write_mask & env->mideleg & sip_writable_mask);
    }

    *ret_value &= env->mideleg;
    return ret;
}

/* Supervisor Protection and Translation */
static int read_satp(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!riscv_feature(env, RISCV_FEATURE_MMU)) {
        *val = 0;
        return 0;
    }

    if (env->priv == PRV_S && get_field(env->mstatus, MSTATUS_TVM)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    } else {
        *val = env->satp;
    }

    return 0;
}

static int write_satp(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!riscv_feature(env, RISCV_FEATURE_MMU)) {
        return 0;
    }
    if (validate_vm(env, get_field(val, SATP_MODE)) &&
        ((val ^ env->satp) & (SATP_MODE | SATP_ASID | SATP_PPN)))
    {
        if (env->priv == PRV_S && get_field(env->mstatus, MSTATUS_TVM)) {
            return -RISCV_EXCP_ILLEGAL_INST;
        } else {
            if ((val ^ env->satp) & SATP_ASID) {
                tlb_flush(env_cpu(env));
            }
            env->satp = val;
        }
    }
    return 0;
}

/* Hypervisor Extensions */
static int read_hstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->hstatus;
    if (!riscv_cpu_is_32bit(env)) {
        /* We only support 64-bit VSXL */
        *val = set_field(*val, HSTATUS_VSXL, 2);
    }
    /* We only support little endian */
    *val = set_field(*val, HSTATUS_VSBE, 0);
    return 0;
}

static int write_hstatus(CPURISCVState *env, int csrno, target_ulong val)
{
    env->hstatus = val;
    if (!riscv_cpu_is_32bit(env) && get_field(val, HSTATUS_VSXL) != 2) {
        qemu_log_mask(LOG_UNIMP, "QEMU does not support mixed HSXLEN options.");
    }
    if (get_field(val, HSTATUS_VSBE) != 0) {
        qemu_log_mask(LOG_UNIMP, "QEMU does not support big endian guests.");
    }
    return 0;
}

static int read_hedeleg(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->hedeleg;
    return 0;
}

static int write_hedeleg(CPURISCVState *env, int csrno, target_ulong val)
{
    env->hedeleg = val;
    return 0;
}

static int read_hideleg(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->hideleg;
    return 0;
}

static int write_hideleg(CPURISCVState *env, int csrno, target_ulong val)
{
    env->hideleg = val;
    return 0;
}

static int rmw_hvip(CPURISCVState *env, int csrno, target_ulong *ret_value,
                   target_ulong new_value, target_ulong write_mask)
{
    int ret = rmw_mip(env, 0, ret_value, new_value,
                      write_mask & hip_writable_mask);

    *ret_value &= hip_writable_mask;

    return ret;
}

static int rmw_hip(CPURISCVState *env, int csrno, target_ulong *ret_value,
                   target_ulong new_value, target_ulong write_mask)
{
    int ret = rmw_mip(env, 0, ret_value, new_value,
                      write_mask & hip_writable_mask);

    *ret_value &= hip_writable_mask;

    return ret;
}

static int read_hie(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mie & VS_MODE_INTERRUPTS;
    return 0;
}

static int write_hie(CPURISCVState *env, int csrno, target_ulong val)
{
    target_ulong newval = (env->mie & ~VS_MODE_INTERRUPTS) | (val & VS_MODE_INTERRUPTS);
    return write_mie(env, CSR_MIE, newval);
}

static int read_hcounteren(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->hcounteren;
    return 0;
}

static int write_hcounteren(CPURISCVState *env, int csrno, target_ulong val)
{
    env->hcounteren = val;
    return 0;
}

static int read_hgeie(CPURISCVState *env, int csrno, target_ulong *val)
{
    qemu_log_mask(LOG_UNIMP, "No support for a non-zero GEILEN.");
    return 0;
}

static int write_hgeie(CPURISCVState *env, int csrno, target_ulong val)
{
    qemu_log_mask(LOG_UNIMP, "No support for a non-zero GEILEN.");
    return 0;
}

static int read_htval(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->htval;
    return 0;
}

static int write_htval(CPURISCVState *env, int csrno, target_ulong val)
{
    env->htval = val;
    return 0;
}

static int read_htinst(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->htinst;
    return 0;
}

static int write_htinst(CPURISCVState *env, int csrno, target_ulong val)
{
    return 0;
}

static int read_hgeip(CPURISCVState *env, int csrno, target_ulong *val)
{
    qemu_log_mask(LOG_UNIMP, "No support for a non-zero GEILEN.");
    return 0;
}

static int write_hgeip(CPURISCVState *env, int csrno, target_ulong val)
{
    qemu_log_mask(LOG_UNIMP, "No support for a non-zero GEILEN.");
    return 0;
}

static int read_hgatp(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->hgatp;
    return 0;
}

static int write_hgatp(CPURISCVState *env, int csrno, target_ulong val)
{
    env->hgatp = val;
    return 0;
}

static int read_htimedelta(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!env->rdtime_fn) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    *val = env->htimedelta;
    return 0;
}

static int write_htimedelta(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!env->rdtime_fn) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    if (riscv_cpu_is_32bit(env)) {
        env->htimedelta = deposit64(env->htimedelta, 0, 32, (uint64_t)val);
    } else {
        env->htimedelta = val;
    }
    return 0;
}

static int read_htimedeltah(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!env->rdtime_fn) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    *val = env->htimedelta >> 32;
    return 0;
}

static int write_htimedeltah(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!env->rdtime_fn) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    env->htimedelta = deposit64(env->htimedelta, 32, 32, (uint64_t)val);
    return 0;
}

/* Virtual CSR Registers */
static int read_vsstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vsstatus;
    return 0;
}

static int write_vsstatus(CPURISCVState *env, int csrno, target_ulong val)
{
    uint64_t mask = (target_ulong)-1;
    env->vsstatus = (env->vsstatus & ~mask) | (uint64_t)val;
    return 0;
}

static int rmw_vsip(CPURISCVState *env, int csrno, target_ulong *ret_value,
                    target_ulong new_value, target_ulong write_mask)
{
    int ret = rmw_mip(env, 0, ret_value, new_value,
                      write_mask & env->mideleg & vsip_writable_mask);
    return ret;
}

static int read_vsie(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mie & env->mideleg & VS_MODE_INTERRUPTS;
    return 0;
}

static int write_vsie(CPURISCVState *env, int csrno, target_ulong val)
{
    target_ulong newval = (env->mie & ~env->mideleg) | (val & env->mideleg & MIP_VSSIP);
    return write_mie(env, CSR_MIE, newval);
}

static int read_vstvec(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vstvec;
    return 0;
}

static int write_vstvec(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vstvec = val;
    return 0;
}

static int read_vsscratch(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vsscratch;
    return 0;
}

static int write_vsscratch(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vsscratch = val;
    return 0;
}

static int read_vsepc(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vsepc;
    return 0;
}

static int write_vsepc(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vsepc = val;
    return 0;
}

static int read_vscause(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vscause;
    return 0;
}

static int write_vscause(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vscause = val;
    return 0;
}

static int read_vstval(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vstval;
    return 0;
}

static int write_vstval(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vstval = val;
    return 0;
}

static int read_vsatp(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vsatp;
    return 0;
}

static int write_vsatp(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vsatp = val;
    return 0;
}

static int read_mtval2(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mtval2;
    return 0;
}

static int write_mtval2(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mtval2 = val;
    return 0;
}

static int read_mtinst(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mtinst;
    return 0;
}

static int write_mtinst(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mtinst = val;
    return 0;
}

/* Physical Memory Protection */
static int read_pmpcfg(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = pmpcfg_csr_read(env, csrno - CSR_PMPCFG0);
    return 0;
}

static int write_pmpcfg(CPURISCVState *env, int csrno, target_ulong val)
{
    pmpcfg_csr_write(env, csrno - CSR_PMPCFG0, val);
    return 0;
}

static int read_pmpaddr(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = pmpaddr_csr_read(env, csrno - CSR_PMPADDR0);
    return 0;
}

static int write_pmpaddr(CPURISCVState *env, int csrno, target_ulong val)
{
    pmpaddr_csr_write(env, csrno - CSR_PMPADDR0, val);
    return 0;
}

/*
 * Functions to access Pointer Masking feature registers
 * We have to check if current priv lvl could modify
 * csr in given mode
 */
static int check_pm_current_disabled(CPURISCVState *env, int csrno)
{
    int csr_priv = get_field(csrno, 0xC00);
    /*
     * If priv lvls differ that means we're accessing csr from higher priv lvl,
     * so allow the access
     */
    if (env->priv != csr_priv) {
        return 0;
    }
    int cur_bit_pos;
    switch (env->priv) {
    case PRV_M:
        /* m-mode is always allowed to modify registers, so allow */
        return 0;
    case PRV_S:
        cur_bit_pos = S_PM_CURRENT;
        break;
    case PRV_U:
        cur_bit_pos = U_PM_CURRENT;
        break;
    default:
        g_assert_not_reached();
    }
    int pm_current = get_field(env->mmte, cur_bit_pos);
    /* It's same priv lvl, so we allow to modify csr only if pm_current==1 */
    return !pm_current;
}

static int read_mmte(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!riscv_has_ext(env, RVJ)) {
        *val = 0;
        return 0;
    }
    *val = env->mmte & MMTE_MASK;
    return 0;
}

static int write_mmte(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!riscv_has_ext(env, RVJ)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    target_ulong wpri_val = val & MMTE_MASK;
    if (val != wpri_val) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "MMTE: WPRI violation written 0x%lx vs expected 0x%lx\n",
                      val, wpri_val);
    }
    env->mmte = val;
    env->mstatus |= MSTATUS_XS | MSTATUS_SD;
    env->mmte |= PM_EXT_DIRTY;
    return 0;
}

static int read_smte(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!riscv_has_ext(env, RVJ)) {
        *val = 0;
        return 0;
    }
    *val = env->mmte & SMTE_MASK;
    return 0;
}

static int write_smte(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!riscv_has_ext(env, RVJ)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    target_ulong wpri_val = val & SMTE_MASK;
    if (val != wpri_val) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SMTE: WPRI violation written 0x%lx vs expected 0x%lx\n",
                      val, wpri_val);
    }
    if (check_pm_current_disabled(env, csrno)) {
        return 0;
    }
    target_ulong new_val = val | (env->mmte & ~SMTE_MASK);
    write_mmte(env, csrno, new_val);
    return 0;
}

static int read_umte(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!riscv_has_ext(env, RVJ)) {
        *val = 0;
        return 0;
    }
    *val = env->mmte & UMTE_MASK;
    return 0;
}

static int write_umte(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!riscv_has_ext(env, RVJ)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    target_ulong wpri_val = val & UMTE_MASK;
    if (val != wpri_val) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "UMTE: WPRI violation written 0x%lx vs expected 0x%lx\n",
                      val, wpri_val);
    }
    if (check_pm_current_disabled(env, csrno)) {
        return 0;
    }
    target_ulong new_val = val | (env->mmte & ~UMTE_MASK);
    write_mmte(env, csrno, new_val);
    return 0;
}

static int read_mpmmask(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!riscv_has_ext(env, RVJ)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    *val = env->mpmmask;
    return 0;
}

static int write_mpmmask(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!riscv_has_ext(env, RVJ)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    env->mpmmask = val;
    env->mstatus |= MSTATUS_XS | MSTATUS_SD;
    env->mmte |= PM_EXT_DIRTY;
    return 0;
}

static int read_spmmask(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!riscv_has_ext(env, RVJ)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    *val = env->spmmask;
    return 0;
}

static int write_spmmask(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!riscv_has_ext(env, RVJ)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    if (check_pm_current_disabled(env, csrno)) {
        return 0;
    }
    env->spmmask = val;
    env->mstatus |= MSTATUS_XS | MSTATUS_SD;
    env->mmte |= PM_EXT_DIRTY;
    return 0;
}

static int read_upmmask(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!riscv_has_ext(env, RVJ)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    *val = env->upmmask;
    return 0;
}

static int write_upmmask(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!riscv_has_ext(env, RVJ)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    if (check_pm_current_disabled(env, csrno)) {
        return 0;
    }
    env->upmmask = val;
    env->mstatus |= MSTATUS_XS | MSTATUS_SD;
    env->mmte |= PM_EXT_DIRTY;
    return 0;
}

static int read_mpmbase(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!riscv_has_ext(env, RVJ)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    *val = env->mpmbase;
    return 0;
}

static int write_mpmbase(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!riscv_has_ext(env, RVJ)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    env->mpmbase = val;
    env->mstatus |= MSTATUS_XS | MSTATUS_SD;
    env->mmte |= PM_EXT_DIRTY;
    return 0;
}

static int read_spmbase(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!riscv_has_ext(env, RVJ)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    *val = env->spmbase;
    return 0;
}

static int write_spmbase(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!riscv_has_ext(env, RVJ)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    if (check_pm_current_disabled(env, csrno)) {
        return 0;
    }
    env->spmbase = val;
    env->mstatus |= MSTATUS_XS | MSTATUS_SD;
    env->mmte |= PM_EXT_DIRTY;
    return 0;
}

static int read_upmbase(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!riscv_has_ext(env, RVJ)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    *val = env->upmbase;
    return 0;
}

static int write_upmbase(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!riscv_has_ext(env, RVJ)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    if (check_pm_current_disabled(env, csrno)) {
        return 0;
    }
    env->upmbase = val;
    env->mstatus |= MSTATUS_XS | MSTATUS_SD;
    env->mmte |= PM_EXT_DIRTY;
    return 0;
}

#endif

/*
 * riscv_csrrw - read and/or update control and status register
 *
 * csrr   <->  riscv_csrrw(env, csrno, ret_value, 0, 0);
 * csrrw  <->  riscv_csrrw(env, csrno, ret_value, value, -1);
 * csrrs  <->  riscv_csrrw(env, csrno, ret_value, -1, value);
 * csrrc  <->  riscv_csrrw(env, csrno, ret_value, 0, value);
 */

int riscv_csrrw(CPURISCVState *env, int csrno, target_ulong *ret_value,
                target_ulong new_value, target_ulong write_mask)
{
    int ret;
    target_ulong old_value;
    RISCVCPU *cpu = env_archcpu(env);

    /* check privileges and return -1 if check fails */
#if !defined(CONFIG_USER_ONLY)
    int effective_priv = env->priv;
    int read_only = get_field(csrno, 0xC00) == 3;

    if (riscv_has_ext(env, RVH) &&
        env->priv == PRV_S &&
        !riscv_cpu_virt_enabled(env)) {
        /*
         * We are in S mode without virtualisation, therefore we are in HS Mode.
         * Add 1 to the effective privledge level to allow us to access the
         * Hypervisor CSRs.
         */
        effective_priv++;
    }

    if ((write_mask && read_only) ||
        (!env->debugger && (effective_priv < get_field(csrno, 0x300)))) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
#endif

    /* ensure the CSR extension is enabled. */
    if (!cpu->cfg.ext_icsr) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    /* check predicate */
    if (!csr_ops[csrno].predicate) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    ret = csr_ops[csrno].predicate(env, csrno);
    if (ret < 0) {
        return ret;
    }

    /* execute combined read/write operation if it exists */
    if (csr_ops[csrno].op) {
        return csr_ops[csrno].op(env, csrno, ret_value, new_value, write_mask);
    }

    /* if no accessor exists then return failure */
    if (!csr_ops[csrno].read) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    /* read old value */
    ret = csr_ops[csrno].read(env, csrno, &old_value);
    if (ret < 0) {
        return ret;
    }

    /* write value if writable and write mask set, otherwise drop writes */
    if (write_mask) {
        new_value = (old_value & ~write_mask) | (new_value & write_mask);
        if (csr_ops[csrno].write) {
            ret = csr_ops[csrno].write(env, csrno, new_value);
            if (ret < 0) {
                return ret;
            }
        }
    }

    /* return old value */
    if (ret_value) {
        *ret_value = old_value;
    }

    return 0;
}

/*
 * Debugger support.  If not in user mode, set env->debugger before the
 * riscv_csrrw call and clear it after the call.
 */
int riscv_csrrw_debug(CPURISCVState *env, int csrno, target_ulong *ret_value,
                target_ulong new_value, target_ulong write_mask)
{
    int ret;
#if !defined(CONFIG_USER_ONLY)
    env->debugger = true;
#endif
    ret = riscv_csrrw(env, csrno, ret_value, new_value, write_mask);
#if !defined(CONFIG_USER_ONLY)
    env->debugger = false;
#endif
    return ret;
}

/* Control and Status Register function table */
static riscv_csr_operations csr_ops[CSR_TABLE_SIZE] = {
    /* User Floating-Point CSRs */
    [CSR_FFLAGS] =              { fs,   read_fflags,      write_fflags      },
    [CSR_FRM] =                 { fs,   read_frm,         write_frm         },
    [CSR_FCSR] =                { fs,   read_fcsr,        write_fcsr        },
    /* Vector CSRs */
    [CSR_VSTART] =              { vs,   read_vstart,      write_vstart      },
    [CSR_VXSAT] =               { vs,   read_vxsat,       write_vxsat       },
    [CSR_VXRM] =                { vs,   read_vxrm,        write_vxrm        },
    [CSR_VL] =                  { vs,   read_vl                             },
    [CSR_VTYPE] =               { vs,   read_vtype                          },
    /* User Timers and Counters */
    [CSR_CYCLE] =               { ctr,  read_instret                        },
    [CSR_INSTRET] =             { ctr,  read_instret                        },
    [CSR_CYCLEH] =              { ctr32,  read_instreth                     },
    [CSR_INSTRETH] =            { ctr32,  read_instreth                     },

    /* In privileged mode, the monitor will have to emulate TIME CSRs only if
     * rdtime callback is not provided by machine/platform emulation */
    [CSR_TIME] =                { ctr,  read_time                           },
    [CSR_TIMEH] =               { ctr32,  read_timeh                        },

#if !defined(CONFIG_USER_ONLY)
    /* Machine Timers and Counters */
    [CSR_MCYCLE] =              { any,  read_instret                        },
    [CSR_MINSTRET] =            { any,  read_instret                        },
    [CSR_MCYCLEH] =             { any32,  read_instreth                     },
    [CSR_MINSTRETH] =           { any32,  read_instreth                     },

    /* Machine Information Registers */
    [CSR_MVENDORID] =           { any,  read_zero                           },
    [CSR_MARCHID] =             { any,  read_zero                           },
    [CSR_MIMPID] =              { any,  read_zero                           },
    [CSR_MHARTID] =             { any,  read_mhartid                        },

    /* Machine Trap Setup */
    [CSR_MSTATUS] =             { any,  read_mstatus,     write_mstatus     },
    [CSR_MISA] =                { any,  read_misa,        write_misa        },
    [CSR_MIDELEG] =             { any,  read_mideleg,     write_mideleg     },
    [CSR_MEDELEG] =             { any,  read_medeleg,     write_medeleg     },
    [CSR_MIE] =                 { any,  read_mie,         write_mie         },
    [CSR_MTVEC] =               { any,  read_mtvec,       write_mtvec       },
    [CSR_MCOUNTEREN] =          { any,  read_mcounteren,  write_mcounteren  },

    [CSR_MSTATUSH] =            { any32,  read_mstatush,    write_mstatush  },

    [CSR_MSCOUNTEREN] =         { any,  read_mscounteren, write_mscounteren },

    /* Machine Trap Handling */
    [CSR_MSCRATCH] =            { any,  read_mscratch,    write_mscratch    },
    [CSR_MEPC] =                { any,  read_mepc,        write_mepc        },
    [CSR_MCAUSE] =              { any,  read_mcause,      write_mcause      },
    [CSR_MBADADDR] =            { any,  read_mbadaddr,    write_mbadaddr    },
    [CSR_MIP] =                 { any,  NULL,     NULL,     rmw_mip         },

    /* Supervisor Trap Setup */
    [CSR_SSTATUS] =             { smode, read_sstatus,     write_sstatus     },
    [CSR_SIE] =                 { smode, read_sie,         write_sie         },
    [CSR_STVEC] =               { smode, read_stvec,       write_stvec       },
    [CSR_SCOUNTEREN] =          { smode, read_scounteren,  write_scounteren  },

    /* Supervisor Trap Handling */
    [CSR_SSCRATCH] =            { smode, read_sscratch,    write_sscratch    },
    [CSR_SEPC] =                { smode, read_sepc,        write_sepc        },
    [CSR_SCAUSE] =              { smode, read_scause,      write_scause      },
    [CSR_SBADADDR] =            { smode, read_sbadaddr,    write_sbadaddr    },
    [CSR_SIP] =                 { smode, NULL,     NULL,     rmw_sip         },

    /* Supervisor Protection and Translation */
    [CSR_SATP] =                { smode, read_satp,        write_satp        },

    [CSR_HSTATUS] =             { hmode,   read_hstatus,     write_hstatus    },
    [CSR_HEDELEG] =             { hmode,   read_hedeleg,     write_hedeleg    },
    [CSR_HIDELEG] =             { hmode,   read_hideleg,     write_hideleg    },
    [CSR_HVIP] =                { hmode,   NULL,     NULL,     rmw_hvip       },
    [CSR_HIP] =                 { hmode,   NULL,     NULL,     rmw_hip        },
    [CSR_HIE] =                 { hmode,   read_hie,         write_hie        },
    [CSR_HCOUNTEREN] =          { hmode,   read_hcounteren,  write_hcounteren },
    [CSR_HGEIE] =               { hmode,   read_hgeie,       write_hgeie      },
    [CSR_HTVAL] =               { hmode,   read_htval,       write_htval      },
    [CSR_HTINST] =              { hmode,   read_htinst,      write_htinst     },
    [CSR_HGEIP] =               { hmode,   read_hgeip,       write_hgeip      },
    [CSR_HGATP] =               { hmode,   read_hgatp,       write_hgatp      },
    [CSR_HTIMEDELTA] =          { hmode,   read_htimedelta,  write_htimedelta },
    [CSR_HTIMEDELTAH] =         { hmode32,   read_htimedeltah, write_htimedeltah},

    [CSR_VSSTATUS] =            { hmode,   read_vsstatus,    write_vsstatus   },
    [CSR_VSIP] =                { hmode,   NULL,     NULL,     rmw_vsip       },
    [CSR_VSIE] =                { hmode,   read_vsie,        write_vsie       },
    [CSR_VSTVEC] =              { hmode,   read_vstvec,      write_vstvec     },
    [CSR_VSSCRATCH] =           { hmode,   read_vsscratch,   write_vsscratch  },
    [CSR_VSEPC] =               { hmode,   read_vsepc,       write_vsepc      },
    [CSR_VSCAUSE] =             { hmode,   read_vscause,     write_vscause    },
    [CSR_VSTVAL] =              { hmode,   read_vstval,      write_vstval     },
    [CSR_VSATP] =               { hmode,   read_vsatp,       write_vsatp      },

    [CSR_MTVAL2] =              { hmode,   read_mtval2,      write_mtval2     },
    [CSR_MTINST] =              { hmode,   read_mtinst,      write_mtinst     },

    /* Physical Memory Protection */
    [CSR_PMPCFG0  ... CSR_PMPCFG3]   = { pmp,   read_pmpcfg,  write_pmpcfg   },
    [CSR_PMPADDR0 ... CSR_PMPADDR15] = { pmp,   read_pmpaddr, write_pmpaddr  },

    /* User Pointer Masking */
    [CSR_UMTE] =                { umode,  read_umte,        write_umte        },
    [CSR_UPMMASK] =             { umode,  read_upmmask,     write_upmmask     },
    [CSR_UPMBASE] =             { umode,  read_upmbase,     write_upmbase     },

    /* Machine Pointer Masking */
    [CSR_MMTE] =                { any,  read_mmte,        write_mmte        },
    [CSR_MPMMASK] =             { any,  read_mpmmask,     write_mpmmask     },
    [CSR_MPMBASE] =             { any,  read_mpmbase,     write_mpmbase     },

    /* Supervisor Pointer Masking */
    [CSR_SMTE] =                { smode, read_smte,        write_smte        },
    [CSR_SPMMASK] =             { smode, read_spmmask,     write_spmmask     },
    [CSR_SPMBASE] =             { smode, read_spmbase,     write_spmbase     },

    /* Performance Counters */
    [CSR_HPMCOUNTER3   ... CSR_HPMCOUNTER31] =    { ctr,  read_zero          },
    [CSR_MHPMCOUNTER3  ... CSR_MHPMCOUNTER31] =   { any,  read_zero          },
    [CSR_MHPMEVENT3    ... CSR_MHPMEVENT31] =     { any,  read_zero          },
    [CSR_HPMCOUNTER3H  ... CSR_HPMCOUNTER31H] =   { ctr32,  read_zero        },
    [CSR_MHPMCOUNTER3H ... CSR_MHPMCOUNTER31H] =  { any32,  read_zero        },
#endif /* !CONFIG_USER_ONLY */
};
