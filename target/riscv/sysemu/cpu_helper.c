/*
 * RISC-V CPU system helpers for QEMU.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "exec/exec-all.h"
#include "cpu.h"
#include "internals.h"
#include "sysemu/cpu-timers.h"
#include "sysemu/pmu.h"
#include "sysemu/instmap.h"
#include "semihosting/common-semi.h"
#include "trace.h"


/*
 * The HS-mode is allowed to configure priority only for the
 * following VS-mode local interrupts:
 *
 * 0  (Reserved interrupt, reads as zero)
 * 1  Supervisor software interrupt
 * 4  (Reserved interrupt, reads as zero)
 * 5  Supervisor timer interrupt
 * 8  (Reserved interrupt, reads as zero)
 * 13 (Reserved interrupt)
 * 14 "
 * 15 "
 * 16 "
 * 17 "
 * 18 "
 * 19 "
 * 20 "
 * 21 "
 * 22 "
 * 23 "
 */

static const int hviprio_index2irq[] = {
    0, 1, 4, 5, 8, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 };
static const int hviprio_index2rdzero[] = {
    1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

int riscv_cpu_hviprio_index2irq(int index, int *out_irq, int *out_rdzero)
{
    if (index < 0 || ARRAY_SIZE(hviprio_index2irq) <= index) {
        return -EINVAL;
    }

    if (out_irq) {
        *out_irq = hviprio_index2irq[index];
    }

    if (out_rdzero) {
        *out_rdzero = hviprio_index2rdzero[index];
    }

    return 0;
}

/*
 * Default priorities of local interrupts are defined in the
 * RISC-V Advanced Interrupt Architecture specification.
 *
 * ----------------------------------------------------------------
 *  Default  |
 *  Priority | Major Interrupt Numbers
 * ----------------------------------------------------------------
 *  Highest  | 47, 23, 46, 45, 22, 44,
 *           | 43, 21, 42, 41, 20, 40
 *           |
 *           | 11 (0b),  3 (03),  7 (07)
 *           |  9 (09),  1 (01),  5 (05)
 *           | 12 (0c)
 *           | 10 (0a),  2 (02),  6 (06)
 *           |
 *           | 39, 19, 38, 37, 18, 36,
 *  Lowest   | 35, 17, 34, 33, 16, 32
 * ----------------------------------------------------------------
 */
static const uint8_t default_iprio[64] = {
    /* Custom interrupts 48 to 63 */
    [63] = IPRIO_MMAXIPRIO,
    [62] = IPRIO_MMAXIPRIO,
    [61] = IPRIO_MMAXIPRIO,
    [60] = IPRIO_MMAXIPRIO,
    [59] = IPRIO_MMAXIPRIO,
    [58] = IPRIO_MMAXIPRIO,
    [57] = IPRIO_MMAXIPRIO,
    [56] = IPRIO_MMAXIPRIO,
    [55] = IPRIO_MMAXIPRIO,
    [54] = IPRIO_MMAXIPRIO,
    [53] = IPRIO_MMAXIPRIO,
    [52] = IPRIO_MMAXIPRIO,
    [51] = IPRIO_MMAXIPRIO,
    [50] = IPRIO_MMAXIPRIO,
    [49] = IPRIO_MMAXIPRIO,
    [48] = IPRIO_MMAXIPRIO,

    /* Custom interrupts 24 to 31 */
    [31] = IPRIO_MMAXIPRIO,
    [30] = IPRIO_MMAXIPRIO,
    [29] = IPRIO_MMAXIPRIO,
    [28] = IPRIO_MMAXIPRIO,
    [27] = IPRIO_MMAXIPRIO,
    [26] = IPRIO_MMAXIPRIO,
    [25] = IPRIO_MMAXIPRIO,
    [24] = IPRIO_MMAXIPRIO,

    [47] = IPRIO_DEFAULT_UPPER,
    [23] = IPRIO_DEFAULT_UPPER + 1,
    [46] = IPRIO_DEFAULT_UPPER + 2,
    [45] = IPRIO_DEFAULT_UPPER + 3,
    [22] = IPRIO_DEFAULT_UPPER + 4,
    [44] = IPRIO_DEFAULT_UPPER + 5,

    [43] = IPRIO_DEFAULT_UPPER + 6,
    [21] = IPRIO_DEFAULT_UPPER + 7,
    [42] = IPRIO_DEFAULT_UPPER + 8,
    [41] = IPRIO_DEFAULT_UPPER + 9,
    [20] = IPRIO_DEFAULT_UPPER + 10,
    [40] = IPRIO_DEFAULT_UPPER + 11,

    [11] = IPRIO_DEFAULT_M,
    [3]  = IPRIO_DEFAULT_M + 1,
    [7]  = IPRIO_DEFAULT_M + 2,

    [9]  = IPRIO_DEFAULT_S,
    [1]  = IPRIO_DEFAULT_S + 1,
    [5]  = IPRIO_DEFAULT_S + 2,

    [12] = IPRIO_DEFAULT_SGEXT,

    [10] = IPRIO_DEFAULT_VS,
    [2]  = IPRIO_DEFAULT_VS + 1,
    [6]  = IPRIO_DEFAULT_VS + 2,

    [39] = IPRIO_DEFAULT_LOWER,
    [19] = IPRIO_DEFAULT_LOWER + 1,
    [38] = IPRIO_DEFAULT_LOWER + 2,
    [37] = IPRIO_DEFAULT_LOWER + 3,
    [18] = IPRIO_DEFAULT_LOWER + 4,
    [36] = IPRIO_DEFAULT_LOWER + 5,

    [35] = IPRIO_DEFAULT_LOWER + 6,
    [17] = IPRIO_DEFAULT_LOWER + 7,
    [34] = IPRIO_DEFAULT_LOWER + 8,
    [33] = IPRIO_DEFAULT_LOWER + 9,
    [16] = IPRIO_DEFAULT_LOWER + 10,
    [32] = IPRIO_DEFAULT_LOWER + 11,
};

uint8_t riscv_cpu_default_priority(int irq)
{
    if (irq < 0 || irq > 63) {
        return IPRIO_MMAXIPRIO;
    }

    return default_iprio[irq] ? default_iprio[irq] : IPRIO_MMAXIPRIO;
};

int riscv_cpu_pending_to_irq(CPURISCVState *env,
                             int extirq, unsigned int extirq_def_prio,
                             uint64_t pending, uint8_t *iprio)
{
    int irq, best_irq = RISCV_EXCP_NONE;
    unsigned int prio, best_prio = UINT_MAX;

    if (!pending) {
        return RISCV_EXCP_NONE;
    }

    irq = ctz64(pending);
    if (!((extirq == IRQ_M_EXT) ? riscv_cpu_cfg(env)->ext_smaia :
                                  riscv_cpu_cfg(env)->ext_ssaia)) {
        return irq;
    }

    pending = pending >> irq;
    while (pending) {
        prio = iprio[irq];
        if (!prio) {
            if (irq == extirq) {
                prio = extirq_def_prio;
            } else {
                prio = (riscv_cpu_default_priority(irq) < extirq_def_prio) ?
                       1 : IPRIO_MMAXIPRIO;
            }
        }
        if ((pending & 0x1) && (prio <= best_prio)) {
            best_irq = irq;
            best_prio = prio;
        }
        irq++;
        pending = pending >> 1;
    }

    return best_irq;
}

uint64_t riscv_cpu_all_pending(CPURISCVState *env)
{
    uint32_t gein = get_field(env->hstatus, HSTATUS_VGEIN);
    uint64_t vsgein = (env->hgeip & (1ULL << gein)) ? MIP_VSEIP : 0;
    uint64_t vstip = (env->vstime_irq) ? MIP_VSTIP : 0;

    return (env->mip | vsgein | vstip) & env->mie;
}

int riscv_cpu_mirq_pending(CPURISCVState *env)
{
    uint64_t irqs = riscv_cpu_all_pending(env) & ~env->mideleg &
                    ~(MIP_SGEIP | MIP_VSSIP | MIP_VSTIP | MIP_VSEIP);

    return riscv_cpu_pending_to_irq(env, IRQ_M_EXT, IPRIO_DEFAULT_M,
                                    irqs, env->miprio);
}

int riscv_cpu_sirq_pending(CPURISCVState *env)
{
    uint64_t irqs = riscv_cpu_all_pending(env) & env->mideleg &
                    ~(MIP_VSSIP | MIP_VSTIP | MIP_VSEIP);

    return riscv_cpu_pending_to_irq(env, IRQ_S_EXT, IPRIO_DEFAULT_S,
                                    irqs, env->siprio);
}

int riscv_cpu_vsirq_pending(CPURISCVState *env)
{
    uint64_t irqs = riscv_cpu_all_pending(env) & env->mideleg &
                    (MIP_VSSIP | MIP_VSTIP | MIP_VSEIP);

    return riscv_cpu_pending_to_irq(env, IRQ_S_EXT, IPRIO_DEFAULT_S,
                                    irqs >> 1, env->hviprio);
}

/* Return true is floating point support is currently enabled */
bool riscv_cpu_fp_enabled(CPURISCVState *env)
{
    if (env->mstatus & MSTATUS_FS) {
        if (env->virt_enabled && !(env->mstatus_hs & MSTATUS_FS)) {
            return false;
        }
        return true;
    }

    return false;
}

/* Return true is vector support is currently enabled */
bool riscv_cpu_vector_enabled(CPURISCVState *env)
{
    if (env->mstatus & MSTATUS_VS) {
        if (env->virt_enabled && !(env->mstatus_hs & MSTATUS_VS)) {
            return false;
        }
        return true;
    }

    return false;
}

void riscv_cpu_swap_hypervisor_regs(CPURISCVState *env)
{
    uint64_t mstatus_mask = MSTATUS_MXR | MSTATUS_SUM |
                            MSTATUS_SPP | MSTATUS_SPIE | MSTATUS_SIE |
                            MSTATUS64_UXL | MSTATUS_VS;

    if (riscv_has_ext(env, RVF)) {
        mstatus_mask |= MSTATUS_FS;
    }
    bool current_virt = env->virt_enabled;

    g_assert(riscv_has_ext(env, RVH));

    if (current_virt) {
        /* Current V=1 and we are about to change to V=0 */
        env->vsstatus = env->mstatus & mstatus_mask;
        env->mstatus &= ~mstatus_mask;
        env->mstatus |= env->mstatus_hs;

        env->vstvec = env->stvec;
        env->stvec = env->stvec_hs;

        env->vsscratch = env->sscratch;
        env->sscratch = env->sscratch_hs;

        env->vsepc = env->sepc;
        env->sepc = env->sepc_hs;

        env->vscause = env->scause;
        env->scause = env->scause_hs;

        env->vstval = env->stval;
        env->stval = env->stval_hs;

        env->vsatp = env->satp;
        env->satp = env->satp_hs;
    } else {
        /* Current V=0 and we are about to change to V=1 */
        env->mstatus_hs = env->mstatus & mstatus_mask;
        env->mstatus &= ~mstatus_mask;
        env->mstatus |= env->vsstatus;

        env->stvec_hs = env->stvec;
        env->stvec = env->vstvec;

        env->sscratch_hs = env->sscratch;
        env->sscratch = env->vsscratch;

        env->sepc_hs = env->sepc;
        env->sepc = env->vsepc;

        env->scause_hs = env->scause;
        env->scause = env->vscause;

        env->stval_hs = env->stval;
        env->stval = env->vstval;

        env->satp_hs = env->satp;
        env->satp = env->vsatp;
    }
}

target_ulong riscv_cpu_get_geilen(CPURISCVState *env)
{
    if (!riscv_has_ext(env, RVH)) {
        return 0;
    }

    return env->geilen;
}

void riscv_cpu_set_geilen(CPURISCVState *env, target_ulong geilen)
{
    if (!riscv_has_ext(env, RVH)) {
        return;
    }

    if (geilen > (TARGET_LONG_BITS - 1)) {
        return;
    }

    env->geilen = geilen;
}

/* This function can only be called to set virt when RVH is enabled */
void riscv_cpu_set_virt_enabled(CPURISCVState *env, bool enable)
{
    /* Flush the TLB on all virt mode changes. */
    if (env->virt_enabled != enable) {
        tlb_flush(env_cpu(env));
    }

    env->virt_enabled = enable;

    if (enable) {
        /*
         * The guest external interrupts from an interrupt controller are
         * delivered only when the Guest/VM is running (i.e. V=1). This means
         * any guest external interrupt which is triggered while the Guest/VM
         * is not running (i.e. V=0) will be missed on QEMU resulting in guest
         * with sluggish response to serial console input and other I/O events.
         *
         * To solve this, we check and inject interrupt after setting V=1.
         */
        riscv_cpu_update_mip(env, 0, 0);
    }
}

int riscv_cpu_claim_interrupts(RISCVCPU *cpu, uint64_t interrupts)
{
    CPURISCVState *env = &cpu->env;
    if (env->miclaim & interrupts) {
        return -1;
    } else {
        env->miclaim |= interrupts;
        return 0;
    }
}

uint64_t riscv_cpu_update_mip(CPURISCVState *env, uint64_t mask,
                              uint64_t value)
{
    CPUState *cs = env_cpu(env);
    uint64_t gein, vsgein = 0, vstip = 0, old = env->mip;

    if (env->virt_enabled) {
        gein = get_field(env->hstatus, HSTATUS_VGEIN);
        vsgein = (env->hgeip & (1ULL << gein)) ? MIP_VSEIP : 0;
    }

    vstip = env->vstime_irq ? MIP_VSTIP : 0;

    QEMU_IOTHREAD_LOCK_GUARD();

    env->mip = (env->mip & ~mask) | (value & mask);

    if (env->mip | vsgein | vstip) {
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }

    return old;
}

void riscv_cpu_set_rdtime_fn(CPURISCVState *env, uint64_t (*fn)(void *),
                             void *arg)
{
    env->rdtime_fn = fn;
    env->rdtime_fn_arg = arg;
}

void riscv_cpu_set_aia_ireg_rmw_fn(CPURISCVState *env, uint32_t priv,
                                   int (*rmw_fn)(void *arg,
                                                 target_ulong reg,
                                                 target_ulong *val,
                                                 target_ulong new_val,
                                                 target_ulong write_mask),
                                   void *rmw_fn_arg)
{
    if (priv <= PRV_M) {
        env->aia_ireg_rmw_fn[priv] = rmw_fn;
        env->aia_ireg_rmw_fn_arg[priv] = rmw_fn_arg;
    }
}

void riscv_cpu_set_mode(CPURISCVState *env, target_ulong newpriv)
{
    g_assert(newpriv <= PRV_M && newpriv != PRV_RESERVED);

    if (icount_enabled() && newpriv != env->priv) {
        riscv_itrigger_update_priv(env);
    }
    /* tlb_flush is unnecessary as mode is contained in mmu_idx */
    env->priv = newpriv;
    env->xl = cpu_recompute_xl(env);
    riscv_cpu_update_mask(env);

    /*
     * Clear the load reservation - otherwise a reservation placed in one
     * context/process can be used by another, resulting in an SC succeeding
     * incorrectly. Version 2.2 of the ISA specification explicitly requires
     * this behaviour, while later revisions say that the kernel "should" use
     * an SC instruction to force the yielding of a load reservation on a
     * preemptive context switch. As a result, do both.
     */
    env->load_res = -1;
}

static target_ulong riscv_transformed_insn(CPURISCVState *env,
                                           target_ulong insn,
                                           target_ulong taddr)
{
    target_ulong xinsn = 0;
    target_ulong access_rs1 = 0, access_imm = 0, access_size = 0;

    /*
     * Only Quadrant 0 and Quadrant 2 of RVC instruction space need to
     * be uncompressed. The Quadrant 1 of RVC instruction space need
     * not be transformed because these instructions won't generate
     * any load/store trap.
     */

    if ((insn & 0x3) != 0x3) {
        /* Transform 16bit instruction into 32bit instruction */
        switch (GET_C_OP(insn)) {
        case OPC_RISC_C_OP_QUAD0: /* Quadrant 0 */
            switch (GET_C_FUNC(insn)) {
            case OPC_RISC_C_FUNC_FLD_LQ:
                if (riscv_cpu_xlen(env) != 128) { /* C.FLD (RV32/64) */
                    xinsn = OPC_RISC_FLD;
                    xinsn = SET_RD(xinsn, GET_C_RS2S(insn));
                    access_rs1 = GET_C_RS1S(insn);
                    access_imm = GET_C_LD_IMM(insn);
                    access_size = 8;
                }
                break;
            case OPC_RISC_C_FUNC_LW: /* C.LW */
                xinsn = OPC_RISC_LW;
                xinsn = SET_RD(xinsn, GET_C_RS2S(insn));
                access_rs1 = GET_C_RS1S(insn);
                access_imm = GET_C_LW_IMM(insn);
                access_size = 4;
                break;
            case OPC_RISC_C_FUNC_FLW_LD:
                if (riscv_cpu_xlen(env) == 32) { /* C.FLW (RV32) */
                    xinsn = OPC_RISC_FLW;
                    xinsn = SET_RD(xinsn, GET_C_RS2S(insn));
                    access_rs1 = GET_C_RS1S(insn);
                    access_imm = GET_C_LW_IMM(insn);
                    access_size = 4;
                } else { /* C.LD (RV64/RV128) */
                    xinsn = OPC_RISC_LD;
                    xinsn = SET_RD(xinsn, GET_C_RS2S(insn));
                    access_rs1 = GET_C_RS1S(insn);
                    access_imm = GET_C_LD_IMM(insn);
                    access_size = 8;
                }
                break;
            case OPC_RISC_C_FUNC_FSD_SQ:
                if (riscv_cpu_xlen(env) != 128) { /* C.FSD (RV32/64) */
                    xinsn = OPC_RISC_FSD;
                    xinsn = SET_RS2(xinsn, GET_C_RS2S(insn));
                    access_rs1 = GET_C_RS1S(insn);
                    access_imm = GET_C_SD_IMM(insn);
                    access_size = 8;
                }
                break;
            case OPC_RISC_C_FUNC_SW: /* C.SW */
                xinsn = OPC_RISC_SW;
                xinsn = SET_RS2(xinsn, GET_C_RS2S(insn));
                access_rs1 = GET_C_RS1S(insn);
                access_imm = GET_C_SW_IMM(insn);
                access_size = 4;
                break;
            case OPC_RISC_C_FUNC_FSW_SD:
                if (riscv_cpu_xlen(env) == 32) { /* C.FSW (RV32) */
                    xinsn = OPC_RISC_FSW;
                    xinsn = SET_RS2(xinsn, GET_C_RS2S(insn));
                    access_rs1 = GET_C_RS1S(insn);
                    access_imm = GET_C_SW_IMM(insn);
                    access_size = 4;
                } else { /* C.SD (RV64/RV128) */
                    xinsn = OPC_RISC_SD;
                    xinsn = SET_RS2(xinsn, GET_C_RS2S(insn));
                    access_rs1 = GET_C_RS1S(insn);
                    access_imm = GET_C_SD_IMM(insn);
                    access_size = 8;
                }
                break;
            default:
                break;
            }
            break;
        case OPC_RISC_C_OP_QUAD2: /* Quadrant 2 */
            switch (GET_C_FUNC(insn)) {
            case OPC_RISC_C_FUNC_FLDSP_LQSP:
                if (riscv_cpu_xlen(env) != 128) { /* C.FLDSP (RV32/64) */
                    xinsn = OPC_RISC_FLD;
                    xinsn = SET_RD(xinsn, GET_C_RD(insn));
                    access_rs1 = 2;
                    access_imm = GET_C_LDSP_IMM(insn);
                    access_size = 8;
                }
                break;
            case OPC_RISC_C_FUNC_LWSP: /* C.LWSP */
                xinsn = OPC_RISC_LW;
                xinsn = SET_RD(xinsn, GET_C_RD(insn));
                access_rs1 = 2;
                access_imm = GET_C_LWSP_IMM(insn);
                access_size = 4;
                break;
            case OPC_RISC_C_FUNC_FLWSP_LDSP:
                if (riscv_cpu_xlen(env) == 32) { /* C.FLWSP (RV32) */
                    xinsn = OPC_RISC_FLW;
                    xinsn = SET_RD(xinsn, GET_C_RD(insn));
                    access_rs1 = 2;
                    access_imm = GET_C_LWSP_IMM(insn);
                    access_size = 4;
                } else { /* C.LDSP (RV64/RV128) */
                    xinsn = OPC_RISC_LD;
                    xinsn = SET_RD(xinsn, GET_C_RD(insn));
                    access_rs1 = 2;
                    access_imm = GET_C_LDSP_IMM(insn);
                    access_size = 8;
                }
                break;
            case OPC_RISC_C_FUNC_FSDSP_SQSP:
                if (riscv_cpu_xlen(env) != 128) { /* C.FSDSP (RV32/64) */
                    xinsn = OPC_RISC_FSD;
                    xinsn = SET_RS2(xinsn, GET_C_RS2(insn));
                    access_rs1 = 2;
                    access_imm = GET_C_SDSP_IMM(insn);
                    access_size = 8;
                }
                break;
            case OPC_RISC_C_FUNC_SWSP: /* C.SWSP */
                xinsn = OPC_RISC_SW;
                xinsn = SET_RS2(xinsn, GET_C_RS2(insn));
                access_rs1 = 2;
                access_imm = GET_C_SWSP_IMM(insn);
                access_size = 4;
                break;
            case 7:
                if (riscv_cpu_xlen(env) == 32) { /* C.FSWSP (RV32) */
                    xinsn = OPC_RISC_FSW;
                    xinsn = SET_RS2(xinsn, GET_C_RS2(insn));
                    access_rs1 = 2;
                    access_imm = GET_C_SWSP_IMM(insn);
                    access_size = 4;
                } else { /* C.SDSP (RV64/RV128) */
                    xinsn = OPC_RISC_SD;
                    xinsn = SET_RS2(xinsn, GET_C_RS2(insn));
                    access_rs1 = 2;
                    access_imm = GET_C_SDSP_IMM(insn);
                    access_size = 8;
                }
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }

        /*
         * Clear Bit1 of transformed instruction to indicate that
         * original insruction was a 16bit instruction
         */
        xinsn &= ~((target_ulong)0x2);
    } else {
        /* Transform 32bit (or wider) instructions */
        switch (MASK_OP_MAJOR(insn)) {
        case OPC_RISC_ATOMIC:
            xinsn = insn;
            access_rs1 = GET_RS1(insn);
            access_size = 1 << GET_FUNCT3(insn);
            break;
        case OPC_RISC_LOAD:
        case OPC_RISC_FP_LOAD:
            xinsn = SET_I_IMM(insn, 0);
            access_rs1 = GET_RS1(insn);
            access_imm = GET_IMM(insn);
            access_size = 1 << GET_FUNCT3(insn);
            break;
        case OPC_RISC_STORE:
        case OPC_RISC_FP_STORE:
            xinsn = SET_S_IMM(insn, 0);
            access_rs1 = GET_RS1(insn);
            access_imm = GET_STORE_IMM(insn);
            access_size = 1 << GET_FUNCT3(insn);
            break;
        case OPC_RISC_SYSTEM:
            if (MASK_OP_SYSTEM(insn) == OPC_RISC_HLVHSV) {
                xinsn = insn;
                access_rs1 = GET_RS1(insn);
                access_size = 1 << ((GET_FUNCT7(insn) >> 1) & 0x3);
                access_size = 1 << access_size;
            }
            break;
        default:
            break;
        }
    }

    if (access_size) {
        xinsn = SET_RS1(xinsn, (taddr - (env->gpr[access_rs1] + access_imm)) &
                               (access_size - 1));
    }

    return xinsn;
}

/*
 * Handle Traps
 *
 * Adapted from Spike's processor_t::take_trap.
 *
 */
void riscv_cpu_do_interrupt(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    bool write_gva = false;
    uint64_t s;

    /*
     * cs->exception is 32-bits wide unlike mcause which is XLEN-bits wide
     * so we mask off the MSB and separate into trap type and cause.
     */
    bool async = !!(cs->exception_index & RISCV_EXCP_INT_FLAG);
    target_ulong cause = cs->exception_index & RISCV_EXCP_INT_MASK;
    uint64_t deleg = async ? env->mideleg : env->medeleg;
    target_ulong tval = 0;
    target_ulong tinst = 0;
    target_ulong htval = 0;
    target_ulong mtval2 = 0;

    if  (cause == RISCV_EXCP_SEMIHOST) {
        do_common_semihosting(cs);
        env->pc += 4;
        return;
    }

    if (!async) {
        /* set tval to badaddr for traps with address information */
        switch (cause) {
        case RISCV_EXCP_LOAD_GUEST_ACCESS_FAULT:
        case RISCV_EXCP_STORE_GUEST_AMO_ACCESS_FAULT:
        case RISCV_EXCP_LOAD_ADDR_MIS:
        case RISCV_EXCP_STORE_AMO_ADDR_MIS:
        case RISCV_EXCP_LOAD_ACCESS_FAULT:
        case RISCV_EXCP_STORE_AMO_ACCESS_FAULT:
        case RISCV_EXCP_LOAD_PAGE_FAULT:
        case RISCV_EXCP_STORE_PAGE_FAULT:
            write_gva = env->two_stage_lookup;
            tval = env->badaddr;
            if (env->two_stage_indirect_lookup) {
                /*
                 * special pseudoinstruction for G-stage fault taken while
                 * doing VS-stage page table walk.
                 */
                tinst = (riscv_cpu_xlen(env) == 32) ? 0x00002000 : 0x00003000;
            } else {
                /*
                 * The "Addr. Offset" field in transformed instruction is
                 * non-zero only for misaligned access.
                 */
                tinst = riscv_transformed_insn(env, env->bins, tval);
            }
            break;
        case RISCV_EXCP_INST_GUEST_PAGE_FAULT:
        case RISCV_EXCP_INST_ADDR_MIS:
        case RISCV_EXCP_INST_ACCESS_FAULT:
        case RISCV_EXCP_INST_PAGE_FAULT:
            write_gva = env->two_stage_lookup;
            tval = env->badaddr;
            if (env->two_stage_indirect_lookup) {
                /*
                 * special pseudoinstruction for G-stage fault taken while
                 * doing VS-stage page table walk.
                 */
                tinst = (riscv_cpu_xlen(env) == 32) ? 0x00002000 : 0x00003000;
            }
            break;
        case RISCV_EXCP_ILLEGAL_INST:
        case RISCV_EXCP_VIRT_INSTRUCTION_FAULT:
            tval = env->bins;
            break;
        case RISCV_EXCP_BREAKPOINT:
            if (cs->watchpoint_hit) {
                tval = cs->watchpoint_hit->hitaddr;
                cs->watchpoint_hit = NULL;
            }
            break;
        default:
            break;
        }
        /* ecall is dispatched as one cause so translate based on mode */
        if (cause == RISCV_EXCP_U_ECALL) {
            assert(env->priv <= 3);

            if (env->priv == PRV_M) {
                cause = RISCV_EXCP_M_ECALL;
            } else if (env->priv == PRV_S && env->virt_enabled) {
                cause = RISCV_EXCP_VS_ECALL;
            } else if (env->priv == PRV_S && !env->virt_enabled) {
                cause = RISCV_EXCP_S_ECALL;
            } else if (env->priv == PRV_U) {
                cause = RISCV_EXCP_U_ECALL;
            }
        }
    }

    trace_riscv_trap(env->mhartid, async, cause, env->pc, tval,
                     riscv_cpu_get_trap_name(cause, async));

    qemu_log_mask(CPU_LOG_INT,
                  "%s: hart:"TARGET_FMT_ld", async:%d, cause:"TARGET_FMT_lx", "
                  "epc:0x"TARGET_FMT_lx", tval:0x"TARGET_FMT_lx", desc=%s\n",
                  __func__, env->mhartid, async, cause, env->pc, tval,
                  riscv_cpu_get_trap_name(cause, async));

    if (env->priv <= PRV_S &&
            cause < TARGET_LONG_BITS && ((deleg >> cause) & 1)) {
        /* handle the trap in S-mode */
        if (riscv_has_ext(env, RVH)) {
            uint64_t hdeleg = async ? env->hideleg : env->hedeleg;

            if (env->virt_enabled && ((hdeleg >> cause) & 1)) {
                /* Trap to VS mode */
                /*
                 * See if we need to adjust cause. Yes if its VS mode interrupt
                 * no if hypervisor has delegated one of hs mode's interrupt
                 */
                if (cause == IRQ_VS_TIMER || cause == IRQ_VS_SOFT ||
                    cause == IRQ_VS_EXT) {
                    cause = cause - 1;
                }
                write_gva = false;
            } else if (env->virt_enabled) {
                /* Trap into HS mode, from virt */
                riscv_cpu_swap_hypervisor_regs(env);
                env->hstatus = set_field(env->hstatus, HSTATUS_SPVP,
                                         env->priv);
                env->hstatus = set_field(env->hstatus, HSTATUS_SPV, true);

                htval = env->guest_phys_fault_addr;

                riscv_cpu_set_virt_enabled(env, 0);
            } else {
                /* Trap into HS mode */
                env->hstatus = set_field(env->hstatus, HSTATUS_SPV, false);
                htval = env->guest_phys_fault_addr;
            }
            env->hstatus = set_field(env->hstatus, HSTATUS_GVA, write_gva);
        }

        s = env->mstatus;
        s = set_field(s, MSTATUS_SPIE, get_field(s, MSTATUS_SIE));
        s = set_field(s, MSTATUS_SPP, env->priv);
        s = set_field(s, MSTATUS_SIE, 0);
        env->mstatus = s;
        env->scause = cause | ((target_ulong)async << (TARGET_LONG_BITS - 1));
        env->sepc = env->pc;
        env->stval = tval;
        env->htval = htval;
        env->htinst = tinst;
        env->pc = (env->stvec >> 2 << 2) +
                  ((async && (env->stvec & 3) == 1) ? cause * 4 : 0);
        riscv_cpu_set_mode(env, PRV_S);
    } else {
        /* handle the trap in M-mode */
        if (riscv_has_ext(env, RVH)) {
            if (env->virt_enabled) {
                riscv_cpu_swap_hypervisor_regs(env);
            }
            env->mstatus = set_field(env->mstatus, MSTATUS_MPV,
                                     env->virt_enabled);
            if (env->virt_enabled && tval) {
                env->mstatus = set_field(env->mstatus, MSTATUS_GVA, 1);
            }

            mtval2 = env->guest_phys_fault_addr;

            /* Trapping to M mode, virt is disabled */
            riscv_cpu_set_virt_enabled(env, 0);
        }

        s = env->mstatus;
        s = set_field(s, MSTATUS_MPIE, get_field(s, MSTATUS_MIE));
        s = set_field(s, MSTATUS_MPP, env->priv);
        s = set_field(s, MSTATUS_MIE, 0);
        env->mstatus = s;
        env->mcause = cause | ~(((target_ulong)-1) >> async);
        env->mepc = env->pc;
        env->mtval = tval;
        env->mtval2 = mtval2;
        env->mtinst = tinst;
        env->pc = (env->mtvec >> 2 << 2) +
                  ((async && (env->mtvec & 3) == 1) ? cause * 4 : 0);
        riscv_cpu_set_mode(env, PRV_M);
    }

    /*
     * NOTE: it is not necessary to yield load reservations here. It is only
     * necessary for an SC from "another hart" to cause a load reservation
     * to be yielded. Refer to the memory consistency model section of the
     * RISC-V ISA Specification.
     */

    env->two_stage_lookup = false;
    env->two_stage_indirect_lookup = false;
}
