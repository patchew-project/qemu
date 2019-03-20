/*
 *  RX translation
 *
 *  Copyright (c) 2019 Yoshinori Sato
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
#include "qemu/bswap.h"
#include "cpu.h"
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg-op.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/translator.h"
#include "trace-tcg.h"
#include "exec/log.h"

typedef struct DisasContext {
    DisasContextBase base;
    CPURXState *env;
    uint32_t pc;
} DisasContext;

typedef struct DisasCompare {
    TCGv value;
    TCGv temp;
    TCGCond cond;
} DisasCompare;

/* Target-specific values for dc->base.is_jmp.  */
#define DISAS_JUMP    DISAS_TARGET_0
#define DISAS_UPDATE  DISAS_TARGET_1
#define DISAS_EXIT    DISAS_TARGET_2

/* global register indexes */
static TCGv cpu_regs[16];
static TCGv cpu_psw, cpu_psw_o, cpu_psw_s, cpu_psw_z, cpu_psw_c;
static TCGv cpu_psw_i, cpu_psw_pm, cpu_psw_u, cpu_psw_ipl;
static TCGv cpu_usp, cpu_fpsw, cpu_bpsw, cpu_bpc, cpu_isp;
static TCGv cpu_fintv, cpu_intb, cpu_pc;
static TCGv_i64 cpu_acc;

#include "exec/gen-icount.h"

/* decoder helper */
static uint32_t decode_load_bytes(DisasContext *ctx, uint32_t insn,
                           int i, int n)
{
    while (++i <= n) {
        uint8_t b = cpu_ldub_code(ctx->env, ctx->base.pc_next++);
        insn |= b << (32 - i * 8);
    }
    return insn;
}

static uint32_t li(DisasContext *ctx, int sz)
{
    int32_t tmp, addr;
    CPURXState *env = ctx->env;
    addr = ctx->base.pc_next;

    switch (sz) {
    case 1:
        ctx->base.pc_next += 1;
        return cpu_ldsb_code(env, addr);
    case 2:
        ctx->base.pc_next += 2;
        return cpu_ldsw_code(env, addr);
    case 3:
        ctx->base.pc_next += 3;
        tmp = cpu_ldsb_code(env, addr + 2) << 16;
        tmp |= cpu_lduw_code(env, addr) & 0xffff;
        return tmp;
    case 0:
        ctx->base.pc_next += 4;
        return cpu_ldl_code(env, addr);
    default:
        g_assert_not_reached();
    }
}

static int dsp16(DisasContext *ctx, int d)
{
    /* dsp16 is always little-endian */
    return bswap_16(d);
}

static int bdsp_s(DisasContext *ctx, int d)
{
    if (d < 3) {
        d += 8;
    }
    return d;
}

static int8_t bdsp_b(DisasContext *ctx, int d)
{
    /* Sign extended */
    return d;
}

static int16_t bdsp_w(DisasContext *ctx, int d)
{
    /* always little-endian and sign extended */
    return bswap_16(d);
}

static int32_t bdsp_a(DisasContext *ctx, int d)
{
    if ((d & 0xff) > 0x80) {
        d = (d << 8) | 0xff;
    } else {
        d <<= 8;
    }
    return bswap_32(d);
}

/* Include the auto-generated decoder. */
#include "decode.inc.c"

void rx_cpu_dump_state(CPUState *cs, FILE *f,
                           fprintf_function cpu_fprintf, int flags)
{
    RXCPU *cpu = RXCPU(cs);
    CPURXState *env = &cpu->env;
    int i;
    uint32_t psw;

    psw = pack_psw(env);
    cpu_fprintf(f, "pc=0x%08x psw=0x%08x\n",
                env->pc, psw);
    for (i = 0; i < 16; i += 4) {
        cpu_fprintf(f, "r%d=0x%08x r%d=0x%08x r%d=0x%08x r%d=0x%08x\n",
                    i, env->regs[i], i + 1, env->regs[i + 1],
                    i + 2, env->regs[i + 2], i + 3, env->regs[i + 3]);
    }
}

static inline bool use_goto_tb(DisasContext *dc, target_ulong dest)
{
    if (unlikely(dc->base.singlestep_enabled)) {
        return false;
    } else {
        return true;
    }
}

static void gen_goto_tb(DisasContext *dc, int n, target_ulong dest)
{
    if (use_goto_tb(dc, dest)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_i32(cpu_pc, dest);
        tcg_gen_exit_tb(dc->base.tb, n);
    } else {
        tcg_gen_movi_i32(cpu_pc, dest);
        if (dc->base.singlestep_enabled) {
            gen_helper_debug(cpu_env);
        } else {
            tcg_gen_lookup_and_goto_ptr();
        }
    }
    dc->base.is_jmp = DISAS_NORETURN;
}

/* generic load wrapper */
static inline void rx_gen_ld(unsigned int size, TCGv reg, TCGv mem)
{
    tcg_gen_qemu_ld_i32(reg, mem, 0, size | MO_SIGN | MO_TE);
}

/* unsigned load wrapper */
static inline void rx_gen_ldu(unsigned int size, TCGv reg, TCGv mem)
{
    tcg_gen_qemu_ld_i32(reg, mem, 0, size | MO_TE);
}

/* generic store wrapper */
static inline void rx_gen_st(unsigned int size, TCGv reg, TCGv mem)
{
    tcg_gen_qemu_st_i32(reg, mem, 0, size | MO_TE);
}

/* [ri, rb] */
static inline void rx_gen_regindex(DisasContext *ctx, TCGv mem,
                                   int size, int ri, int rb)
{
    tcg_gen_shli_i32(mem, cpu_regs[ri], size);
    tcg_gen_add_i32(mem, mem, cpu_regs[rb]);
}

/* dsp[reg] */
static inline TCGv rx_index_addr(DisasContext *ctx, TCGv mem,
                                 int ld, int size, int reg)
{
    uint32_t dsp;

    switch (ld) {
    case 0:
        return cpu_regs[reg];
    case 1:
        dsp = cpu_ldub_code(ctx->env, ctx->base.pc_next) << size;
        tcg_gen_addi_i32(mem, cpu_regs[reg], dsp);
        ctx->base.pc_next += 1;
        return mem;
    case 2:
        dsp = cpu_lduw_code(ctx->env, ctx->base.pc_next) << size;
        tcg_gen_addi_i32(mem, cpu_regs[reg], dsp);
        ctx->base.pc_next += 2;
        return mem;
    default:
        g_assert_not_reached();
        return NULL;
    }
}

/* load source operand */
static inline TCGv rx_load_source(DisasContext *ctx, TCGv mem,
                                  int ld, int mi, int rs)
{
    TCGv addr;
    if (ld < 3) {
        switch (mi) {
        case 0: /* dsp[rs].b */
        case 1: /* dsp[rs].w */
        case 2: /* dsp[rs].l */
            addr = rx_index_addr(ctx, mem, ld, mi, rs);
            rx_gen_ld(mi, mem, addr);
            break;
        case 3: /* dsp[rs].uw */
        case 4: /* dsp[rs].ub */
            addr = rx_index_addr(ctx, mem, ld, 4 - mi, rs);
            rx_gen_ldu(4 - mi, mem, addr);
            break;
        }
        return mem;
    } else {
        return cpu_regs[rs];
    }
}

/* Processor mode check */
static inline int is_privileged(DisasContext *ctx, int is_exception)
{
    if (extract32(ctx->base.tb->flags, PSW_PM, 1)) {
        if (is_exception) {
            gen_helper_raise_privilege_violation(cpu_env);
        }
        return 0;
    } else {
        return 1;
    }
}

/* generate QEMU condition */
static inline void psw_cond(DisasCompare *dc, uint32_t cond)
{
    switch (cond) {
    case 0: /* z */
        dc->cond = TCG_COND_EQ;
        dc->value = cpu_psw_z;
        break;
    case 1: /* nz */
        dc->cond = TCG_COND_NE;
        dc->value = cpu_psw_z;
        break;
    case 2: /* c */
        dc->cond = TCG_COND_NE;
        dc->value = cpu_psw_c;
        break;
    case 3: /* nc */
        dc->cond = TCG_COND_EQ;
        dc->value = cpu_psw_c;
        break;
    case 4: /* gtu (C& ~Z) == 1 */
    case 5: /* leu (C& ~Z) == 0 */
        tcg_gen_setcondi_i32(TCG_COND_NE, dc->temp, cpu_psw_z, 0);
        tcg_gen_and_i32(dc->temp, dc->temp, cpu_psw_c);
        dc->cond = (cond == 4) ? TCG_COND_NE : TCG_COND_EQ;
        dc->value = dc->temp;
        break;
    case 6: /* pz (S == 0) */
        dc->cond = TCG_COND_GE;
        dc->value = cpu_psw_s;
        break;
    case 7: /* n (S == 1) */
        dc->cond = TCG_COND_LT;
        dc->value = cpu_psw_s;
        break;
    case 8: /* ge (S^O)==0 */
    case 9: /* lt (S^O)==1 */
        tcg_gen_xor_i32(dc->temp, cpu_psw_o, cpu_psw_s);
        dc->cond = (cond == 8) ? TCG_COND_GE : TCG_COND_LT;
        dc->value = dc->temp;
        break;
    case 10: /* gt ((S^O)|Z)==0 */
    case 11: /* le ((S^O)|Z)==1 */
        tcg_gen_xor_i32(dc->temp, cpu_psw_o, cpu_psw_s);
        tcg_gen_sari_i32(dc->temp, dc->temp, 31);
        tcg_gen_not_i32(dc->temp, dc->temp);
        tcg_gen_and_i32(dc->temp, dc->temp, cpu_psw_z);
        dc->cond = (cond == 10) ? TCG_COND_NE : TCG_COND_EQ;
        dc->value = dc->temp;
        break;
    case 12: /* o */
        dc->cond = TCG_COND_LT;
        dc->value = cpu_psw_o;
        break;
    case 13: /* no */
        dc->cond = TCG_COND_GE;
        dc->value = cpu_psw_o;
        break;
    case 14: /* always true */
        dc->cond = TCG_COND_ALWAYS;
        dc->value = dc->temp;
        break;
    case 15: /* always false */
        dc->cond = TCG_COND_NEVER;
        dc->value = dc->temp;
        break;
    default:
        g_assert_not_reached();
        break;
    }
}

/* control registers allow write */
/* Some control registers can only be written in privileged mode. */
static inline int allow_write_cr(DisasContext *ctx, int cr)
{
    return cr < 8 || is_privileged(ctx, 0);
}

/* mov.[bwl] rs,dsp5[rd] */
static bool trans_MOV_rm(DisasContext *ctx, arg_MOV_rm *a)
{
    TCGv mem;
    mem = tcg_temp_new();
    tcg_gen_addi_i32(mem, cpu_regs[a->rd], a->dsp << a->sz);
    rx_gen_st(a->sz, cpu_regs[a->rs], mem);
    tcg_temp_free(mem);
    return true;
}

/* mov.[bwl] dsp5[rd],rs */
static bool trans_MOV_mr(DisasContext *ctx, arg_MOV_mr *a)
{
    TCGv mem;
    mem = tcg_temp_new();
    tcg_gen_addi_i32(mem, cpu_regs[a->rd], a->dsp << a->sz);
    rx_gen_ld(a->sz, cpu_regs[a->rs], mem);
    tcg_temp_free(mem);
    return true;
}

/* mov.l #uimm4,rd */
/* mov.l #uimm8,rd */
/* mov.l #imm,rd */
static bool trans_MOV_ir(DisasContext *ctx, arg_MOV_ir *a)
{
    tcg_gen_movi_i32(cpu_regs[a->rd], a->imm);
    return true;
}

/* mov.[bwl] #uimm8,dsp[rd] */
/* mov.[bwl] #imm, dsp[rd] */
static bool trans_MOV_im(DisasContext *ctx, arg_MOV_im *a)
{
    TCGv imm, mem;
    imm = tcg_const_i32(a->imm);
    mem = tcg_temp_new();
    tcg_gen_addi_i32(mem, cpu_regs[a->rd], a->dsp << a->sz);
    rx_gen_st(a->sz, imm, mem);
    tcg_temp_free(imm);
    tcg_temp_free(mem);
    return true;
}

/* mov.[bwl] [ri,rb],rd */
static bool trans_MOV_ar(DisasContext *ctx, arg_MOV_ar *a)
{
    TCGv mem;
    mem = tcg_temp_new();
    rx_gen_regindex(ctx, mem, a->sz, a->ri, a->rb);
    rx_gen_ld(a->sz, cpu_regs[a->rd], mem);
    tcg_temp_free(mem);
    return true;
}

/* mov.[bwl] rd,[ri,rb] */
static bool trans_MOV_ra(DisasContext *ctx, arg_MOV_ra *a)
{
    TCGv mem;
    mem = tcg_temp_new();
    rx_gen_regindex(ctx, mem, a->sz, a->ri, a->rb);
    rx_gen_st(a->sz, cpu_regs[a->rs], mem);
    tcg_temp_free(mem);
    return true;
}

/* mov.[bwl] dsp[rs],dsp[rd] */
/* mov.[bwl] rs,dsp[rd] */
/* mov.[bwl] dsp[rs],rd */
/* mov.[bwl] rs,rd */
static bool trans_MOV_mm(DisasContext *ctx, arg_MOV_mm *a)
{
    static void (* const mov[])(TCGv ret, TCGv arg) = {
        tcg_gen_ext8s_i32, tcg_gen_ext16s_i32, tcg_gen_mov_i32,
    };
    TCGv tmp, mem, addr;
    if (a->lds == 3 && a->ldd == 3) {
        /* mov.[bwl] rs,rd */
        mov[a->sz](cpu_regs[a->rd], cpu_regs[a->rs]);
        return true;
    }

    mem = tcg_temp_new();
    if (a->lds == 3) {
        /* mov.[bwl] rs,dsp[rd] */
        addr = rx_index_addr(ctx, mem, a->ldd, a->sz, a->rs);
        rx_gen_st(a->sz, cpu_regs[a->rd], addr);
    } else if (a->ldd == 3) {
        /* mov.[bwl] dsp[rs],rd */
        addr = rx_index_addr(ctx, mem, a->lds, a->sz, a->rs);
        rx_gen_ld(a->sz, cpu_regs[a->rd], addr);
    } else {
        /* mov.[bwl] dsp[rs],dsp[rd] */
        tmp = tcg_temp_new();
        addr = rx_index_addr(ctx, mem, a->lds, a->sz, a->rs);
        rx_gen_ld(a->sz, tmp, addr);
        addr = rx_index_addr(ctx, mem, a->ldd, a->sz, a->rd);
        rx_gen_st(a->sz, tmp, addr);
        tcg_temp_free(tmp);
    }
    tcg_temp_free(mem);
    return true;
}

typedef void (*ldstfn)(unsigned int sz, TCGv val, TCGv mem);
static inline void MOV_prrp(ldstfn ldst, int ad, int sz, int rs, int rd)
{
    TCGv temp;
    if (rs == rd) {
        temp = tcg_temp_new();
        tcg_gen_mov_i32(temp, cpu_regs[rs]);
    } else {
        temp = cpu_regs[rs];
    }
    if (ad == 1) {
        tcg_gen_subi_i32(cpu_regs[rd], cpu_regs[rd], 1 << sz);
    }
    ldst(sz, temp, cpu_regs[rd]);
    if (ad == 0 && rs != rd) {
        tcg_gen_addi_i32(cpu_regs[rd], cpu_regs[rd], 1 << sz);
    }
    if (rs == rd) {
        tcg_temp_free(temp);
    }
}

/* mov.[bwl] rs,[rd+] */
/* mov.[bwl] rs,[-rd] */
static bool trans_MOV_rp(DisasContext *ctx, arg_MOV_rp *a)
{
    MOV_prrp(rx_gen_st, a->ad, a->sz, a->rs, a->rd);
    return true;
}

/* mov.[bwl] [rd+],rs */
/* mov.[bwl] [-rd],rs */
static bool trans_MOV_pr(DisasContext *ctx, arg_MOV_pr *a)
{
    MOV_prrp(rx_gen_ld, a->ad, a->sz, a->rs, a->rd);
    return true;
}

/* movu.[bw] dsp5[rs],rd */
/* movu.[bw] dsp[rs],rd */
static bool trans_MOVU_mr(DisasContext *ctx, arg_MOVU_mr *a)
{
    TCGv mem;
    mem = tcg_temp_new();
    tcg_gen_addi_i32(mem, cpu_regs[a->rs], a->dsp << a->sz);
    rx_gen_ldu(a->sz, cpu_regs[a->rd], mem);
    tcg_temp_free(mem);
    return true;
}

/* movu.[bw] rs,rd */
static bool trans_MOVU_rr(DisasContext *ctx, arg_MOVU_rr *a)
{
    static void (* const ext[])(TCGv ret, TCGv arg) = {
        tcg_gen_ext8u_i32, tcg_gen_ext16u_i32,
    };
    ext[a->sz](cpu_regs[a->rd], cpu_regs[a->rs]);
    return true;
}

/* movu.[bw] [ri,rb],rd */
static bool trans_MOVU_ar(DisasContext *ctx, arg_MOVU_ar *a)
{
    TCGv mem;
    mem = tcg_temp_new();
    rx_gen_regindex(ctx, mem, a->sz, a->ri, a->rb);
    rx_gen_ldu(a->sz, cpu_regs[a->rd], mem);
    tcg_temp_free(mem);
    return true;
}

/* movu.[bw] [rs+],rd */
/* movu.[bw] [-rs],rd */
static bool trans_MOVU_pr(DisasContext *ctx, arg_MOVU_pr *a)
{
    MOV_prrp(rx_gen_ldu, a->ad, a->sz, a->rd, a->rs);
    return true;
}

/* pop rd */
static bool trans_POP(DisasContext *ctx, arg_POP *a)
{
    rx_gen_ld(MO_32, cpu_regs[a->rd], cpu_regs[0]);
    if (a->rd != 0) {
        tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], 4);
    }
    return true;
}

/* popc rx */
static bool trans_POPC(DisasContext *ctx, arg_POPC *a)
{
    TCGv cr, val;
    cr = tcg_const_i32(a->cr);
    val = tcg_temp_new();
    rx_gen_ld(MO_32, val, cpu_regs[0]);
    tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], 4);
    if (allow_write_cr(ctx, a->cr)) {
        gen_helper_mvtc(cpu_env, cr, val);
        if (a->cr == 0 && is_privileged(ctx, 0)) {
            /* Since PSW.I may be updated here, */
            ctx->base.is_jmp = DISAS_UPDATE;
        }
    }
    tcg_temp_free(cr);
    tcg_temp_free(val);
    return true;
}

/* popm rd-rd2 */
static bool trans_POPM(DisasContext *ctx, arg_POPM *a)
{
    int r;

    for (r = a->rd; r <= a->rd2; r++) {
        rx_gen_ld(MO_32, cpu_regs[r], cpu_regs[0]);
        tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], 4);
    }
    return true;
}

/* push rs */
static bool trans_PUSH_r(DisasContext *ctx, arg_PUSH_r *a)
{
    TCGv val;
    if (a->rs == 0) {
        /* When pushing r0, push the value before decrementing */
        val = tcg_temp_new();
        tcg_gen_mov_i32(val, cpu_regs[0]);
        tcg_gen_subi_i32(cpu_regs[0], cpu_regs[0], 4);
        rx_gen_st(a->sz, val, cpu_regs[0]);
        tcg_temp_free(val);
    } else {
        tcg_gen_subi_i32(cpu_regs[0], cpu_regs[0], 4);
        rx_gen_st(a->sz, cpu_regs[a->rs], cpu_regs[0]);
    }
    return true;
}

/* push dsp[rs] */
static bool trans_PUSH_m(DisasContext *ctx, arg_PUSH_m *a)
{
    TCGv mem, addr;
    mem = tcg_temp_new();
    addr = rx_index_addr(ctx, mem, a->ld, a->sz, a->rs);
    rx_gen_ld(a->sz, mem, addr);
    tcg_gen_subi_i32(cpu_regs[0], cpu_regs[0], 4);
    rx_gen_st(a->sz, mem, cpu_regs[0]);
    tcg_temp_free(mem);
    return true;
}

/* pushc rx */
static bool trans_PUSHC(DisasContext *ctx, arg_PUSHC *a)
{
    TCGv cr, val;
    cr = tcg_const_i32(a->cr);
    val = tcg_temp_new();
    gen_helper_mvfc(val, cpu_env, cr);
    tcg_gen_subi_i32(cpu_regs[0], cpu_regs[0], 4);
    rx_gen_st(MO_32, val, cpu_regs[0]);
    tcg_temp_free(cr);
    tcg_temp_free(val);
    return true;
}

/* pushm rs-rs2 */
static bool trans_PUSHM(DisasContext *ctx, arg_PUSHM *a)
{
    int r;

    tcg_debug_assert(a->rs > 0);
    for (r = a->rs2; r >= a->rs; r--) {
        tcg_gen_subi_i32(cpu_regs[0], cpu_regs[0], 4);
        rx_gen_st(MO_32, cpu_regs[r], cpu_regs[0]);
    }
    return true;
}

/* xchg rs,rd */
static bool trans_XCHG_rr(DisasContext *ctx, arg_XCHG_rr *a)
{
    TCGv tmp;
    tmp = tcg_temp_new();
    tcg_gen_mov_i32(tmp, cpu_regs[a->rs]);
    tcg_gen_mov_i32(cpu_regs[a->rs], cpu_regs[a->rd]);
    tcg_gen_mov_i32(cpu_regs[a->rd], tmp);
    tcg_temp_free(tmp);
    return true;
}

static inline TCGMemOp mi_to_mop(unsigned mi)
{
    static const TCGMemOp mop[5] = { MO_SB, MO_SW, MO_UL, MO_UW, MO_UB };
    tcg_debug_assert(mi < 5);
    return mop[mi];
}

/* xchg dsp[rs].<mi>,rd */
static bool trans_XCHG_mr(DisasContext *ctx, arg_XCHG_mr *a)
{
    TCGv mem, addr;
    mem = tcg_temp_new();
    switch (a->mi) {
    case 0: /* dsp[rs].b */
    case 1: /* dsp[rs].w */
    case 2: /* dsp[rs].l */
        addr = rx_index_addr(ctx, mem, a->ld, a->mi, a->rs);
        break;
    case 3: /* dsp[rs].uw */
    case 4: /* dsp[rs].ub */
        addr = rx_index_addr(ctx, mem, a->ld, 4 - a->mi, a->rs);
        break;
    }
    tcg_gen_atomic_xchg_i32(cpu_regs[a->rd], addr, cpu_regs[a->rd],
                            0, mi_to_mop(a->mi));
    tcg_temp_free(mem);
    return true;
}

static inline void stcond(TCGCond cond, int rd, int imm)
{
    TCGv z;
    TCGv _imm;
    z = tcg_const_i32(0);
    _imm = tcg_const_i32(imm);
    tcg_gen_movcond_i32(cond, cpu_regs[rd], cpu_psw_z, z,
                        _imm, cpu_regs[rd]);
    tcg_temp_free(z);
    tcg_temp_free(_imm);
}

/* stz #imm,rd */
static bool trans_STZ(DisasContext *ctx, arg_STZ *a)
{
    stcond(TCG_COND_EQ, a->rd, a->imm);
    return true;
}

/* stnz #imm,rd */
static bool trans_STNZ(DisasContext *ctx, arg_STNZ *a)
{
    stcond(TCG_COND_NE, a->rd, a->imm);
    return true;
}

/* sccnd.[bwl] rd */
/* sccnd.[bwl] dsp:[rd] */
static bool trans_SCCnd(DisasContext *ctx, arg_SCCnd *a)
{
    DisasCompare dc;
    TCGv val, mem, addr;
    dc.temp = tcg_temp_new();
    psw_cond(&dc, a->cd);
    if (a->ld < 3) {
        val = tcg_temp_new();
        mem = tcg_temp_new();
        tcg_gen_setcondi_i32(dc.cond, val, dc.value, 0);
        addr = rx_index_addr(ctx, mem, a->sz, a->ld, a->rd);
        rx_gen_st(a->sz, val, addr);
        tcg_temp_free(val);
        tcg_temp_free(mem);
    } else {
        tcg_gen_setcondi_i32(dc.cond, cpu_regs[a->rd], dc.value, 0);
    }
    tcg_temp_free(dc.temp);
    return true;
}

/* rtsd #imm */
static bool trans_RTSD_i(DisasContext *ctx, arg_RTSD_i *a)
{
    tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], a->imm  << 2);
    rx_gen_ld(MO_32, cpu_pc, cpu_regs[0]);
    tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], 4);
    ctx->base.is_jmp = DISAS_JUMP;
    return true;
}

/* rtsd #imm, rd-rd2 */
static bool trans_RTSD_irr(DisasContext *ctx, arg_RTSD_irr *a)
{
    int dst;
    int adj = a->imm - (a->rd2 - a->rd + 1);

    tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], adj << 2);
    for (dst = a->rd; dst <= a->rd2; dst++) {
        rx_gen_ld(MO_32, cpu_regs[dst], cpu_regs[0]);
        tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], 4);
    }
    rx_gen_ld(MO_32, cpu_pc, cpu_regs[0]);
    tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], 4);
    ctx->base.is_jmp = DISAS_JUMP;
    return true;
}

typedef void (*logicfn)(TCGv ret, TCGv arg1, TCGv arg2);
static inline void gen_logic_op(logicfn opr, TCGv ret, TCGv arg1, TCGv arg2)
{
    opr(cpu_psw_z, arg1, arg2);
    tcg_gen_mov_i32(cpu_psw_s, cpu_psw_z);
    if (ret) {
        tcg_gen_mov_i32(ret, cpu_psw_z);
    }
}

typedef void (*logicifn)(TCGv ret, TCGv arg1, int32_t arg2);
static inline void gen_logici_op(logicifn opr, TCGv ret,
                                TCGv arg1, int32_t arg2)
{
    opr(cpu_psw_z, arg1, arg2);
    tcg_gen_mov_i32(cpu_psw_s, cpu_psw_z);
    if (ret) {
        tcg_gen_mov_i32(ret, cpu_psw_z);
    }
}

/* and #uimm:4, rd */
/* and #imm, rd */
static bool trans_AND_ir(DisasContext *ctx, arg_AND_ir *a)
{
    gen_logici_op(tcg_gen_andi_i32,
                  cpu_regs[a->rd], cpu_regs[a->rd], a->imm);
    return true;
}

/* and dsp[rs], rd */
/* and rs,rd */
static bool trans_AND_mr(DisasContext *ctx, arg_AND_mr *a)
{
    TCGv val, mem;
    mem = tcg_temp_new();
    val = rx_load_source(ctx, mem, a->ld, a->mi, a->rs);
    gen_logic_op(tcg_gen_and_i32, cpu_regs[a->rd], cpu_regs[a->rd], val);
    tcg_temp_free(mem);
    return true;
}

/* and rs,rs2,rd */
static bool trans_AND_rrr(DisasContext *ctx, arg_AND_rrr *a)
{
    gen_logic_op(tcg_gen_and_i32, cpu_regs[a->rd],
             cpu_regs[a->rs2], cpu_regs[a->rs]);
    return true;
}

/* or #uimm:4, rd */
/* or #imm, rd */
static bool trans_OR_ir(DisasContext *ctx, arg_OR_ir *a)
{
    gen_logici_op(tcg_gen_ori_i32,
                  cpu_regs[a->rd], cpu_regs[a->rd], a->imm);
    return true;
}

/* or dsp[rs], rd */
/* or rs,rd */
static bool trans_OR_mr(DisasContext *ctx, arg_OR_mr *a)
{
    TCGv val, mem;
    mem = tcg_temp_new();
    val = rx_load_source(ctx, mem, a->ld, a->mi, a->rs);
    gen_logic_op(tcg_gen_or_i32, cpu_regs[a->rd], cpu_regs[a->rd], val);
    tcg_temp_free(mem);
    return true;
}

/* or rs,rs2,rd */
static bool trans_OR_rrr(DisasContext *ctx, arg_OR_rrr *a)
{
    gen_logic_op(tcg_gen_or_i32, cpu_regs[a->rd],
             cpu_regs[a->rs2], cpu_regs[a->rs]);
    return true;
}

/* xor #imm, rd */
static bool trans_XOR_ir(DisasContext *ctx, arg_XOR_ir *a)
{
    gen_logici_op(tcg_gen_xori_i32,
                  cpu_regs[a->rd], cpu_regs[a->rd], a->imm);
    return true;
}

/* xor dsp[rs], rd */
/* xor rs,rd */
static bool trans_XOR_mr(DisasContext *ctx, arg_XOR_mr *a)
{
    TCGv val, mem;
    mem = tcg_temp_new();
    val = rx_load_source(ctx, mem, a->ld, a->mi, a->rs);
    gen_logic_op(tcg_gen_xor_i32, cpu_regs[a->rd], cpu_regs[a->rd], val);
    tcg_temp_free(mem);
    return true;
}

/* tst #imm, rd */
static bool trans_TST_ir(DisasContext *ctx, arg_TST_ir *a)
{
    gen_logici_op(tcg_gen_andi_i32, NULL, cpu_regs[a->rd], a->imm);
    return true;
}

/* tst dsp[rs], rd */
/* tst rs, rd */
static bool trans_TST_mr(DisasContext *ctx, arg_TST_mr *a)
{
    TCGv mem, val;
    mem = tcg_temp_new();
    val = rx_load_source(ctx, mem, a->ld, a->mi, a->rs);
    gen_logic_op(tcg_gen_and_i32, NULL, cpu_regs[a->rd], val);
    tcg_temp_free(mem);
    return true;
}

/* not rd */
/* not rs, rd */
static bool trans_NOT_rr(DisasContext *ctx, arg_NOT_rr *a)
{
    tcg_gen_not_i32(cpu_regs[a->rd], cpu_regs[a->rs]);
    tcg_gen_mov_i32(cpu_psw_z, cpu_regs[a->rd]);
    tcg_gen_mov_i32(cpu_psw_s, cpu_regs[a->rd]);
    return true;
}

/* neg rd */
/* neg rs, rd */
static bool trans_NEG_rr(DisasContext *ctx, arg_NEG_rr *a)
{
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_psw_o, cpu_regs[a->rs], 0x80000000);
    tcg_gen_neg_i32(cpu_regs[a->rd], cpu_regs[a->rs]);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_psw_c, cpu_regs[a->rd], 0);
    tcg_gen_mov_i32(cpu_psw_z, cpu_regs[a->rd]);
    tcg_gen_mov_i32(cpu_psw_s, cpu_regs[a->rd]);
    return true;
}

/* ret = arg1 + arg2 + psw_c */
static void rx_gen_adc_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv cf, z;
    cf = tcg_temp_new();
    z = tcg_const_i32(0);
    tcg_gen_mov_i32(cf, cpu_psw_c);
    tcg_gen_add2_i32(cpu_psw_s, cpu_psw_c, arg1, z, arg2, z);
    tcg_gen_add2_i32(cpu_psw_s, cpu_psw_c, cpu_psw_s, cpu_psw_c, cf, z);
    tcg_gen_mov_i32(cpu_psw_z, cpu_psw_s);
    tcg_gen_xor_i32(cpu_psw_o, cpu_psw_s, arg1);
    tcg_gen_xor_i32(z, arg1, arg2);
    tcg_gen_andc_i32(cpu_psw_o, cpu_psw_o, z);
    tcg_gen_mov_i32(ret, cpu_psw_s);
    tcg_temp_free(z);
}

/* adc #imm, rd */
static bool trans_ADC_ir(DisasContext *ctx, arg_ADC_ir *a)
{
    TCGv imm = tcg_const_i32(a->imm);
    rx_gen_adc_i32(cpu_regs[a->rd], cpu_regs[a->rd], imm);
    tcg_temp_free(imm);
    return true;
}

/* adc rs, rd */
static bool trans_ADC_rr(DisasContext *ctx, arg_ADC_rr *a)
{
    rx_gen_adc_i32(cpu_regs[a->rd], cpu_regs[a->rd], cpu_regs[a->rs]);
    return true;
}

/* adc dsp[rs], rd */
static bool trans_ADC_mr(DisasContext *ctx, arg_ADC_mr *a)
{
    TCGv mem, addr;
    mem = tcg_temp_new();
    addr = rx_index_addr(ctx, mem, a->ld, MO_32, a->rs);
    rx_gen_ld(a->ld, mem, addr);
    rx_gen_adc_i32(cpu_regs[a->rd], cpu_regs[a->rd], mem);
    tcg_temp_free(mem);
    return true;
}

/* ret = arg1 + arg2 */
static void rx_gen_add_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv z;
    z = tcg_const_i32(0);
    tcg_gen_add2_i32(cpu_psw_s, cpu_psw_c, arg1, z, arg2, z);
    tcg_gen_mov_i32(cpu_psw_z, cpu_psw_s);
    tcg_gen_xor_i32(cpu_psw_o, cpu_psw_s, arg1);
    tcg_gen_xor_i32(z, arg1, arg2);
    tcg_gen_andc_i32(cpu_psw_o, cpu_psw_o, z);
    tcg_gen_mov_i32(ret, cpu_psw_s);
    tcg_temp_free(z);
}

/* add #uimm4, rd */
/* add #imm, rs, rd */
static bool trans_ADD_irr(DisasContext *ctx, arg_ADD_irr *a)
{
    TCGv imm = tcg_const_i32(a->imm);
    rx_gen_add_i32(cpu_regs[a->rd], cpu_regs[a->rs2], imm);
    tcg_temp_free(imm);
    return true;
}

/* add rs, rd */
/* add dsp[rs], rd */
static bool trans_ADD_mr(DisasContext *ctx, arg_ADD_mr *a)
{
    TCGv val, mem;
    mem = tcg_temp_new();
    val = rx_load_source(ctx, mem, a->ld, a->mi, a->rs);
    rx_gen_add_i32(cpu_regs[a->rd], cpu_regs[a->rd], val);
    tcg_temp_free(mem);
    return true;
}

/* add rs, rs2, rd */
static bool trans_ADD_rrr(DisasContext *ctx, arg_ADD_rrr *a)
{
    rx_gen_add_i32(cpu_regs[a->rd], cpu_regs[a->rs], cpu_regs[a->rs2]);
    return true;
}

/* ret = arg1 - arg2 */
static void rx_gen_sub_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv temp;
    tcg_gen_sub_i32(cpu_psw_s, arg1, arg2);
    tcg_gen_mov_i32(cpu_psw_z, cpu_psw_s);
    tcg_gen_setcond_i32(TCG_COND_GEU, cpu_psw_c, arg1, arg2);
    tcg_gen_xor_i32(cpu_psw_o, cpu_psw_s, arg1);
    temp = tcg_temp_new_i32();
    tcg_gen_xor_i32(temp, arg1, arg2);
    tcg_gen_and_i32(cpu_psw_o, cpu_psw_o, temp);
    tcg_temp_free_i32(temp);
    if (ret) {
        tcg_gen_mov_i32(ret, cpu_psw_s);
    }
}

/* ret = arg1 - arg2 - !psw_c */
/* -> ret = arg1 + ~arg2 + psw_c */
static void rx_gen_sbb_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv temp;
    temp = tcg_temp_new();
    tcg_gen_not_i32(temp, arg2);
    rx_gen_adc_i32(ret, arg1, temp);
    tcg_temp_free(temp);
}

/* cmp #imm4, rs2 */
/* cmp #imm8, rs2 */
/* cmp #imm, rs2 */
static bool trans_CMP_ir(DisasContext *ctx, arg_CMP_ir *a)
{
    TCGv imm = tcg_const_i32(a->imm);
    rx_gen_sub_i32(NULL, cpu_regs[a->rs2], imm);
    tcg_temp_free(imm);
    return true;
}

/* cmp rs, rs2 */
/* cmp dsp[rs], rs2 */
static bool trans_CMP_mr(DisasContext *ctx, arg_CMP_mr *a)
{
    TCGv val, mem;
    mem = tcg_temp_new();
    val = rx_load_source(ctx, mem, a->ld, a->mi, a->rs);
    rx_gen_sub_i32(NULL, cpu_regs[a->rd], val);
    tcg_temp_free(mem);
    return true;
}

/* sub #imm4, rd */
static bool trans_SUB_ir(DisasContext *ctx, arg_SUB_ir *a)
{
    TCGv imm = tcg_const_i32(a->imm);
    rx_gen_sub_i32(cpu_regs[a->rd], cpu_regs[a->rd], imm);
    tcg_temp_free(imm);
    return true;
}

/* sub rs, rd */
/* sub dsp[rs], rd */
static bool trans_SUB_mr(DisasContext *ctx, arg_SUB_mr *a)
{
    TCGv val, mem;
    mem = tcg_temp_new();
    val = rx_load_source(ctx, mem, a->ld, a->mi, a->rs);
    rx_gen_sub_i32(cpu_regs[a->rd], cpu_regs[a->rd], val);
    tcg_temp_free(mem);
    return true;
}

/* sub rs, rs2, rd */
static bool trans_SUB_rrr(DisasContext *ctx, arg_SUB_rrr *a)
{
    rx_gen_sub_i32(cpu_regs[a->rd], cpu_regs[a->rs2], cpu_regs[a->rs]);
    return true;
}

/* sbb rs, rd */
static bool trans_SBB_rr(DisasContext *ctx, arg_SBB_rr *a)
{
    rx_gen_sbb_i32(cpu_regs[a->rd], cpu_regs[a->rd], cpu_regs[a->rs]);
    return true;
}

/* sbb dsp[rs], rd */
static bool trans_SBB_mr(DisasContext *ctx, arg_SBB_mr *a)
{
    TCGv val, mem;
    mem = tcg_temp_new();
    val = rx_load_source(ctx, mem, a->ld, MO_32, a->rs);
    rx_gen_sbb_i32(cpu_regs[a->rd], cpu_regs[a->rd], val);
    tcg_temp_free(mem);
    return true;
}

/* abs rd */
/* abs rs, rd */
static bool trans_ABS_rr(DisasContext *ctx, arg_ABS_rr *a)
{
    TCGv neg;
    TCGv zero;
    neg = tcg_temp_new();
    zero = tcg_const_i32(0);
    tcg_gen_neg_i32(neg, cpu_regs[a->rs]);
    tcg_gen_movcond_i32(TCG_COND_LT, cpu_regs[a->rd],
                        cpu_regs[a->rs], zero,
                        neg, cpu_regs[a->rs]);
    tcg_temp_free(neg);
    tcg_temp_free(zero);
    return true;
}

/* max #imm, rd */
static bool trans_MAX_ir(DisasContext *ctx, arg_MAX_ir *a)
{
    TCGv imm = tcg_const_i32(a->imm);
    tcg_gen_smax_i32(cpu_regs[a->rd], cpu_regs[a->rd], imm);
    tcg_temp_free(imm);
    return true;
}

/* max rs, rd */
/* max dsp[rs], rd */
static bool trans_MAX_mr(DisasContext *ctx, arg_MAX_mr *a)
{
    TCGv val, mem;
    mem = tcg_temp_new();
    val = rx_load_source(ctx, mem, a->ld, a->mi, a->rs);
    tcg_gen_smax_i32(cpu_regs[a->rd], cpu_regs[a->rd], val);
    tcg_temp_free(mem);
    return true;
}

/* min #imm, rd */
static bool trans_MIN_ir(DisasContext *ctx, arg_MIN_ir *a)
{
    TCGv imm = tcg_const_i32(a->imm);
    tcg_gen_smin_i32(cpu_regs[a->rd], cpu_regs[a->rd], imm);
    tcg_temp_free(imm);
    return true;
}

/* min rs, rd */
/* min dsp[rs], rd */
static bool trans_MIN_mr(DisasContext *ctx, arg_MIN_mr *a)
{
    TCGv val, mem;
    mem = tcg_temp_new();
    val = rx_load_source(ctx, mem, a->ld, a->mi, a->rs);
    tcg_gen_smin_i32(cpu_regs[a->rd], cpu_regs[a->rd], val);
    tcg_temp_free(mem);
    return true;
}

/* mul #uimm4, rd */
/* mul #imm, rd */
static bool trans_MUL_ir(DisasContext *ctx, arg_MUL_ir *a)
{
    tcg_gen_muli_i32(cpu_regs[a->rd], cpu_regs[a->rd], a->imm);
    return true;
}

/* mul rs, rd */
/* mul dsp[rs], rd */
static bool trans_MUL_mr(DisasContext *ctx, arg_MUL_mr *a)
{
    TCGv val, mem;
    mem = tcg_temp_new();
    val = rx_load_source(ctx, mem, a->ld, a->mi, a->rs);
    tcg_gen_mul_i32(cpu_regs[a->rd], cpu_regs[a->rd], val);
    tcg_temp_free(mem);
    return true;
}

/* mul rs, rs2, rd */
static bool trans_MUL_rrr(DisasContext *ctx, arg_MUL_rrr *a)
{
    tcg_gen_mul_i32(cpu_regs[a->rd], cpu_regs[a->rs], cpu_regs[a->rs2]);
    return true;
}

/* emul #imm, rd */
static bool trans_EMUL_ir(DisasContext *ctx, arg_EMUL_ir *a)
{
    TCGv imm = tcg_const_i32(a->imm);
    tcg_gen_muls2_i32(cpu_regs[a->rd], cpu_regs[a->rd + 1],
                      cpu_regs[a->rd], imm);
    tcg_temp_free(imm);
    return true;
}

/* emul rs, rd */
/* emul dsp[rs], rd */
static bool trans_EMUL_mr(DisasContext *ctx, arg_EMUL_mr *a)
{
    TCGv val, mem;
    mem = tcg_temp_new();
    val = rx_load_source(ctx, mem, a->ld, a->mi, a->rs);
    tcg_gen_muls2_i32(cpu_regs[a->rd], cpu_regs[a->rd + 1],
                      cpu_regs[a->rd], val);
    tcg_temp_free(mem);
    return true;
}

/* emulu #imm, rd */
static bool trans_EMULU_ir(DisasContext *ctx, arg_EMULU_ir *a)
{
    TCGv imm = tcg_const_i32(a->imm);
    if (a->rd > 14) {
        g_assert_not_reached();
    }
    tcg_gen_mulu2_i32(cpu_regs[a->rd], cpu_regs[a->rd + 1],
                      cpu_regs[a->rd], imm);
    tcg_temp_free(imm);
    return true;
}

/* emulu rs, rd */
/* emulu dsp[rs], rd */
static bool trans_EMULU_mr(DisasContext *ctx, arg_EMULU_mr *a)
{
    TCGv val, mem;
    if (a->rd > 14) {
        g_assert_not_reached();
    }
    mem = tcg_temp_new();
    val = rx_load_source(ctx, mem, a->ld, a->mi, a->rs);
    tcg_gen_mulu2_i32(cpu_regs[a->rd], cpu_regs[a->rd + 1],
                      cpu_regs[a->rd], val);
    tcg_temp_free(mem);
    return true;
}

/* div #imm, rd */
static bool trans_DIV_ir(DisasContext *ctx, arg_DIV_ir *a)
{
    TCGv imm = tcg_const_i32(a->imm);
    gen_helper_div(cpu_regs[a->rd], cpu_env, cpu_regs[a->rd], imm);
    tcg_temp_free(imm);
    return true;
}

/* div rs, rd */
/* div dsp[rs], rd */
static bool trans_DIV_mr(DisasContext *ctx, arg_DIV_mr *a)
{
    TCGv val, mem;
    mem = tcg_temp_new();
    val = rx_load_source(ctx, mem, a->ld, a->mi, a->rs);
    gen_helper_divu(cpu_regs[a->rd], cpu_env, cpu_regs[a->rd], val);
    tcg_temp_free(mem);
    return true;
}

/* divu #imm, rd */
static bool trans_DIVU_ir(DisasContext *ctx, arg_DIVU_ir *a)
{
    TCGv imm = tcg_const_i32(a->imm);
    gen_helper_divu(cpu_regs[a->rd], cpu_env, cpu_regs[a->rd], imm);
    tcg_temp_free(imm);
    return true;
}

/* divu rs, rd */
/* divu dsp[rs], rd */
static bool trans_DIVU_mr(DisasContext *ctx, arg_DIVU_mr *a)
{
    TCGv val, mem;
    mem = tcg_temp_new();
    val = rx_load_source(ctx, mem, a->ld, a->mi, a->rs);
    gen_helper_divu(cpu_regs[a->rd], cpu_env, cpu_regs[a->rd], val);
    tcg_temp_free(mem);
    return true;
}


/* shll #imm:5, rd */
/* shll #imm:5, rs2, rd */
static bool trans_SHLL_irr(DisasContext *ctx, arg_SHLL_irr *a)
{
    TCGv tmp;
    tmp = tcg_temp_new();
    if (a->imm) {
        tcg_gen_sari_i32(cpu_psw_c, cpu_regs[a->rs2], 32 - a->imm);
        tcg_gen_shli_i32(cpu_regs[a->rd], cpu_regs[a->rs2], a->imm);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_psw_o, cpu_psw_c, 0);
        tcg_gen_setcondi_i32(TCG_COND_EQ, tmp, cpu_psw_c, 0xffffffff);
        tcg_gen_or_i32(cpu_psw_o, cpu_psw_o, tmp);
        tcg_gen_setcondi_i32(TCG_COND_NE, cpu_psw_c, cpu_psw_c, 0);
    } else {
        tcg_gen_movi_i32(cpu_psw_c, 0);
        tcg_gen_movi_i32(cpu_psw_o, 0);
    }
    tcg_gen_mov_i32(cpu_psw_z, cpu_regs[a->rd]);
    tcg_gen_mov_i32(cpu_psw_s, cpu_regs[a->rd]);
    return true;
}

/* shll rs, rd */
static bool trans_SHLL_rr(DisasContext *ctx, arg_SHLL_rr *a)
{
    TCGLabel *l1, *l2;
    TCGv count, tmp;

    l1 = gen_new_label();
    l2 = gen_new_label();
    tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_regs[a->rs], 0, l1);
    count = tcg_const_i32(32);
    tmp = tcg_temp_new();
    tcg_gen_sub_i32(count, count, cpu_regs[a->rs]);
    tcg_gen_sar_i32(cpu_psw_c, cpu_regs[a->rd], count);
    tcg_gen_shl_i32(cpu_regs[a->rd], cpu_regs[a->rd], cpu_regs[a->rs]);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_psw_o, cpu_psw_c, 0);
    tcg_gen_setcondi_i32(TCG_COND_EQ, tmp, cpu_psw_c, 0xffffffff);
    tcg_gen_or_i32(cpu_psw_o, cpu_psw_o, tmp);
    tcg_gen_setcondi_i32(TCG_COND_NE, cpu_psw_c, cpu_psw_c, 0);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_movi_i32(cpu_psw_c, 0);
    tcg_gen_movi_i32(cpu_psw_o, 0);
    gen_set_label(l2);
    tcg_gen_mov_i32(cpu_psw_z, cpu_regs[a->rd]);
    tcg_gen_mov_i32(cpu_psw_s, cpu_regs[a->rd]);
    tcg_temp_free(count);
    tcg_temp_free(tmp);
    return true;
}

static inline void shiftr_imm(uint32_t rd, uint32_t rs, uint32_t imm,
                              unsigned int alith)
{
    static void (* const gen_sXri[])(TCGv ret, TCGv arg1, int arg2) = {
        tcg_gen_shri_i32, tcg_gen_sari_i32,
    };
    tcg_debug_assert(alith < 2);
    if (imm) {
        gen_sXri[alith](cpu_regs[rd], cpu_regs[rs], imm - 1);
        tcg_gen_andi_i32(cpu_psw_c, cpu_regs[rd], 0x00000001);
        gen_sXri[alith](cpu_regs[rd], cpu_regs[rd], 1);
    } else {
        tcg_gen_movi_i32(cpu_psw_c, 0);
    }
    tcg_gen_movi_i32(cpu_psw_o, 0);
    tcg_gen_mov_i32(cpu_psw_z, cpu_regs[rd]);
    tcg_gen_mov_i32(cpu_psw_s, cpu_regs[rd]);
}

static inline void shiftr_reg(uint32_t rd, uint32_t rs, unsigned int alith)
{
    TCGLabel *skipz, *done;
    TCGv count;
    static void (* const gen_sXri[])(TCGv ret, TCGv arg1, int arg2) = {
        tcg_gen_shri_i32, tcg_gen_sari_i32,
    };
    static void (* const gen_sXr[])(TCGv ret, TCGv arg1, TCGv arg2) = {
        tcg_gen_shr_i32, tcg_gen_sar_i32,
    };
    tcg_debug_assert(alith < 2);
    skipz = gen_new_label();
    done = gen_new_label();
    count = tcg_temp_new();
    tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_regs[rs], 0, skipz);
    tcg_gen_subi_i32(count, cpu_regs[rs], 1);
    gen_sXr[alith](cpu_regs[rd], cpu_regs[rd], count);
    tcg_gen_andi_i32(cpu_psw_c, cpu_regs[rd], 0x00000001);
    gen_sXri[alith](cpu_regs[rd], cpu_regs[rd], 1);
    tcg_gen_br(done);
    gen_set_label(skipz);
    tcg_gen_movi_i32(cpu_psw_c, 0);
    gen_set_label(done);
    tcg_gen_movi_i32(cpu_psw_o, 0);
    tcg_gen_mov_i32(cpu_psw_z, cpu_regs[rd]);
    tcg_gen_mov_i32(cpu_psw_s, cpu_regs[rd]);
    tcg_temp_free(count);
}

/* shar #imm:5, rd */
/* shar #imm:5, rs2, rd */
static bool trans_SHAR_irr(DisasContext *ctx, arg_SHAR_irr *a)
{
    shiftr_imm(a->rd, a->rs2, a->imm, 1);
    return true;
}

/* shar rs, rd */
static bool trans_SHAR_rr(DisasContext *ctx, arg_SHAR_rr *a)
{
    shiftr_reg(a->rd, a->rs, 1);
    return true;
}

/* shlr #imm:5, rd */
/* shlr #imm:5, rs2, rd */
static bool trans_SHLR_irr(DisasContext *ctx, arg_SHLR_irr *a)
{
    shiftr_imm(a->rd, a->rs2, a->imm, 0);
    return true;
}

/* shlr rs, rd */
static bool trans_SHLR_rr(DisasContext *ctx, arg_SHLR_rr *a)
{
    shiftr_reg(a->rd, a->rs, 0);
    return true;
}

/* rolc rd*/
static bool trans_ROLC(DisasContext *ctx, arg_ROLC *a)
{
    TCGv tmp;
    tmp = tcg_temp_new();
    tcg_gen_shri_i32(tmp, cpu_regs[a->rd], 31);
    tcg_gen_shli_i32(cpu_regs[a->rd], cpu_regs[a->rd], 1);
    tcg_gen_or_i32(cpu_regs[a->rd], cpu_regs[a->rd], cpu_psw_c);
    tcg_gen_mov_i32(cpu_psw_c, tmp);
    tcg_gen_mov_i32(cpu_psw_z, cpu_regs[a->rd]);
    tcg_gen_mov_i32(cpu_psw_s, cpu_regs[a->rd]);
    tcg_temp_free(tmp);
    return true;
}

/* rorc rd */
static bool trans_RORC(DisasContext *ctx, arg_RORC *a)
{
    TCGv tmp;
    tmp = tcg_temp_new();
    tcg_gen_andi_i32(tmp, cpu_regs[a->rd], 0x00000001);
    tcg_gen_shri_i32(cpu_regs[a->rd], cpu_regs[a->rd], 1);
    tcg_gen_shli_i32(cpu_psw_c, cpu_psw_c, 31);
    tcg_gen_or_i32(cpu_regs[a->rd], cpu_regs[a->rd], cpu_psw_c);
    tcg_gen_mov_i32(cpu_psw_c, tmp);
    tcg_gen_mov_i32(cpu_psw_z, cpu_regs[a->rd]);
    tcg_gen_mov_i32(cpu_psw_s, cpu_regs[a->rd]);
    return true;
}

enum {ROTR = 0, ROTL = 1};
enum {ROT_IMM = 0, ROT_REG = 1};
static inline void rx_rot(int ir, int dir, int rd, int src)
{
    switch (dir) {
    case ROTL:
        if (ir == ROT_IMM) {
            tcg_gen_rotli_i32(cpu_regs[rd], cpu_regs[rd], src);
        } else {
            tcg_gen_rotl_i32(cpu_regs[rd], cpu_regs[rd], cpu_regs[src]);
        }
        tcg_gen_andi_i32(cpu_psw_c, cpu_regs[rd], 0x00000001);
        break;
    case ROTR:
        if (ir == ROT_IMM) {
            tcg_gen_rotri_i32(cpu_regs[rd], cpu_regs[rd], src);
        } else {
            tcg_gen_rotr_i32(cpu_regs[rd], cpu_regs[rd], cpu_regs[src]);
        }
        tcg_gen_shri_i32(cpu_psw_c, cpu_regs[rd], 31);
        break;
    }
    tcg_gen_mov_i32(cpu_psw_z, cpu_regs[rd]);
    tcg_gen_mov_i32(cpu_psw_s, cpu_regs[rd]);
}

/* rotl #imm, rd */
static bool trans_ROTL_ir(DisasContext *ctx, arg_ROTL_ir *a)
{
    rx_rot(ROT_IMM, ROTL, a->rd, a->imm);
    return true;
}

/* rotl rs, rd */
static bool trans_ROTL_rr(DisasContext *ctx, arg_ROTL_rr *a)
{
    rx_rot(ROT_REG, ROTL, a->rd, a->rs);
    return true;
}

/* rotr #imm, rd */
static bool trans_ROTR_ir(DisasContext *ctx, arg_ROTR_ir *a)
{
    rx_rot(ROT_IMM, ROTR, a->rd, a->imm);
    return true;
}

/* rotr rs, rd */
static bool trans_ROTR_rr(DisasContext *ctx, arg_ROTR_rr *a)
{
    rx_rot(ROT_REG, ROTR, a->rd, a->rs);
    return true;
}

/* revl rs, rd */
static bool trans_REVL(DisasContext *ctx, arg_REVL *a)
{
    tcg_gen_bswap32_i32(cpu_regs[a->rd], cpu_regs[a->rs]);
    return true;
}

/* revw rs, rd */
static bool trans_REVW(DisasContext *ctx, arg_REVW *a)
{
    TCGv hi, lo;

    hi = tcg_temp_new();
    lo = tcg_temp_new();
    tcg_gen_shri_i32(hi, cpu_regs[a->rs], 16);
    tcg_gen_bswap16_i32(hi, hi);
    tcg_gen_shli_i32(hi, hi, 16);
    tcg_gen_bswap16_i32(lo, cpu_regs[a->rs]);
    tcg_gen_or_i32(cpu_regs[a->rd], hi, lo);
    tcg_temp_free(hi);
    tcg_temp_free(lo);
    return true;
}

/* conditional branch helper */
static void rx_bcnd_main(DisasContext *ctx, int cd, int dst)
{
    TCGv z, t, f;
    DisasCompare dc;
    switch (cd) {
    case 0 ... 13:
        dc.temp = tcg_temp_new();
        z = tcg_const_i32(0);
        t = tcg_const_i32(ctx->pc + dst);
        f = tcg_const_i32(ctx->base.pc_next);
        psw_cond(&dc, cd);
        tcg_gen_movcond_i32(dc.cond, cpu_pc, dc.value, z, t, f);
        tcg_temp_free(t);
        tcg_temp_free(f);
        tcg_temp_free(dc.temp);
        tcg_temp_free(z);
        break;
    case 14:
        /* always true case */
        tcg_gen_movi_i32(cpu_pc, ctx->pc + dst);
        break;
    case 15:
        /* always false case */
        tcg_gen_movi_i32(cpu_pc, ctx->base.pc_next);
        break;
    }
    ctx->base.is_jmp = DISAS_JUMP;
}

/* beq dsp:3 / bne dsp:3 */
/* beq dsp:8 / bne dsp:8 */
/* bc dsp:8 / bnc dsp:8 */
/* bgtu dsp:8 / bleu dsp:8 */
/* bpz dsp:8 / bn dsp:8 */
/* bge dsp:8 / blt dsp:8 */
/* bgt dsp:8 / ble dsp:8 */
/* bo dsp:8 / bno dsp:8 */
/* beq dsp:16 / bne dsp:16 */
static bool trans_BCnd(DisasContext *ctx, arg_BCnd *a)
{
    rx_bcnd_main(ctx, a->cd, a->dsp);
    return true;
}

/* bra dsp:3 */
/* bra dsp:8 */
/* bra dsp:16 */
/* bra dsp:24 */
static bool trans_BRA(DisasContext *ctx, arg_BRA *a)
{
    rx_bcnd_main(ctx, 14, a->dsp);
    return true;
}

/* bra rs */
static bool trans_BRA_l(DisasContext *ctx, arg_BRA_l *a)
{
    tcg_gen_add_i32(cpu_pc, cpu_pc, cpu_regs[a->rd]);
    ctx->base.is_jmp = DISAS_JUMP;
    return true;
}

static inline void rx_save_pc(DisasContext *ctx)
{
    TCGv pc = tcg_const_i32(ctx->base.pc_next);
    tcg_gen_subi_i32(cpu_regs[0], cpu_regs[0], 4);
    rx_gen_st(MO_32, pc, cpu_regs[0]);
    tcg_temp_free(pc);
}

/* jmp rs */
static bool trans_JMP(DisasContext *ctx, arg_JMP *a)
{
    tcg_gen_mov_i32(cpu_pc, cpu_regs[a->rs]);
    ctx->base.is_jmp = DISAS_JUMP;
    return true;
}

/* jsr rs */
static bool trans_JSR(DisasContext *ctx, arg_JSR *a)
{
    rx_save_pc(ctx);
    tcg_gen_mov_i32(cpu_pc, cpu_regs[a->rs]);
    ctx->base.is_jmp = DISAS_JUMP;
    return true;
}

/* bsr dsp:16 */
/* bsr dsp:24 */
static bool trans_BSR(DisasContext *ctx, arg_BSR *a)
{
    rx_save_pc(ctx);
    rx_bcnd_main(ctx, 14, a->dsp);
    return true;
}

/* bsr rs */
static bool trans_BSR_l(DisasContext *ctx, arg_BSR_l *a)
{
    rx_save_pc(ctx);
    tcg_gen_add_i32(cpu_pc, cpu_pc, cpu_regs[a->rd]);
    ctx->base.is_jmp = DISAS_JUMP;
    return true;
}

/* rts */
static bool trans_RTS(DisasContext *ctx, arg_RTS *a)
{
    rx_gen_ld(MO_32, cpu_pc, cpu_regs[0]);
    tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], 4);
    ctx->base.is_jmp = DISAS_JUMP;
    return true;
}

/* nop */
static bool trans_NOP(DisasContext *ctx, arg_NOP *a)
{
    return true;
}

/* scmpu */
static bool trans_SCMPU(DisasContext *ctx, arg_SCMPU *a)
{
    gen_helper_scmpu(cpu_env);
    return true;
}

/* smovu */
static bool trans_SMOVU(DisasContext *ctx, arg_SMOVU *a)
{
    gen_helper_smovu(cpu_env);
    return true;
}

/* smovf */
static bool trans_SMOVF(DisasContext *ctx, arg_SMOVF *a)
{
    gen_helper_smovf(cpu_env);
    return true;
}

/* smovb */
static bool trans_SMOVB(DisasContext *ctx, arg_SMOVB *a)
{
    gen_helper_smovb(cpu_env);
    return true;
}

#define STRING(op)                              \
    do {                                        \
        TCGv size = tcg_const_i32(a->sz);       \
        gen_helper_##op(cpu_env, size);         \
        tcg_temp_free(size);                    \
    } while (0)

/* suntile */
static bool trans_SUNTIL(DisasContext *ctx, arg_SUNTIL *a)
{
    STRING(suntil);
    return true;
}

/* swhile */
static bool trans_SWHILE(DisasContext *ctx, arg_SWHILE *a)
{
    STRING(swhile);
    return true;
}
/* sstr */
static bool trans_SSTR(DisasContext *ctx, arg_SSTR *a)
{
    STRING(sstr);
    return true;
}

/* rmpa */
static bool trans_RMPA(DisasContext *ctx, arg_RMPA *a)
{
    STRING(rmpa);
    return true;
}

#define MULMAC(op)                                            \
    do {                                                      \
        TCGv regs = tcg_const_i32(a->rs << 4 | a->rs2);       \
        gen_helper_##op(cpu_env, regs);                       \
        tcg_temp_free(regs);                                  \
    } while (0)

/* mulhi rs,rs2 */
static bool trans_MULHI(DisasContext *ctx, arg_MULHI *a)
{
    MULMAC(mulhi);
    return true;
}

/* mullo rs,rs2 */
static bool trans_MULLO(DisasContext *ctx, arg_MULLO *a)
{
    MULMAC(mullo);
    return true;
}

/* machi rs,rs2 */
static bool trans_MACHI(DisasContext *ctx, arg_MACHI *a)
{
    MULMAC(machi);
    return true;
}

/* maclo rs,rs2 */
static bool trans_MACLO(DisasContext *ctx, arg_MACLO *a)
{
    MULMAC(maclo);
    return true;
}

/* mvfachi rd */
static bool trans_MVFACHI(DisasContext *ctx, arg_MVFACHI *a)
{
    tcg_gen_extrh_i64_i32(cpu_regs[a->rd], cpu_acc);
    return true;
}

/* mvfacmi rd */
static bool trans_MVFACMI(DisasContext *ctx, arg_MVFACMI *a)
{
    TCGv_i64 rd64;
    rd64 = tcg_temp_new_i64();
    tcg_gen_extract_i64(rd64, cpu_acc, 16, 32);
    tcg_gen_extrl_i64_i32(cpu_regs[a->rd], rd64);
    tcg_temp_free_i64(rd64);
    return true;
}

/* mvtachi rs */
static bool trans_MVTACHI(DisasContext *ctx, arg_MVTACHI *a)
{
    TCGv_i64 rs64;
    rs64 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(rs64, cpu_regs[a->rs]);
    tcg_gen_deposit_i64(cpu_acc, cpu_acc, rs64, 32, 32);
    tcg_temp_free_i64(rs64);
    return true;
}

/* mvtaclo rs */
static bool trans_MVTACLO(DisasContext *ctx, arg_MVTACLO *a)
{
    TCGv_i64 rs64;
    rs64 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(rs64, cpu_regs[a->rs]);
    tcg_gen_deposit_i64(cpu_acc, cpu_acc, rs64, 0, 32);
    tcg_temp_free_i64(rs64);
    return true;
}

/* racw #imm */
static bool trans_RACW(DisasContext *ctx, arg_RACW *a)
{
    TCGv imm = tcg_const_i32(a->imm + 1);
    gen_helper_racw(cpu_env, imm);
    tcg_temp_free(imm);
    return true;
}

/* sat rd */
static bool trans_SAT(DisasContext *ctx, arg_SAT *a)
{
    TCGv rd = tcg_const_i32(a->rd);
    gen_helper_sat(cpu_env, rd);
    tcg_temp_free(rd);
    return true;
}

/* satr */
static bool trans_SATR(DisasContext *ctx, arg_SATR *a)
{
    gen_helper_satr(cpu_env);
    return true;
}

#define cat3(a, b, c) a##b##c
#define FOP(name, op)                                                   \
    static bool cat3(trans_, name, _ir)(DisasContext *ctx,              \
                                        cat3(arg_, name, _ir) * a)      \
    {                                                                   \
        TCGv imm = tcg_const_i32(li(ctx, 0));                           \
        gen_helper_##op(cpu_regs[a->rd], cpu_env,                       \
                        cpu_regs[a->rd], imm);                          \
        tcg_temp_free(imm);                                             \
        return true;                                                    \
    }                                                                   \
    static bool cat3(trans_, name, _mr)(DisasContext *ctx,              \
                                        cat3(arg_, name, _mr) * a)      \
    {                                                                   \
        TCGv val, mem;                                                  \
        mem = tcg_temp_new();                                           \
        val = rx_load_source(ctx, mem, a->ld, MO_32, a->rs);            \
        gen_helper_##op(cpu_regs[a->rd], cpu_env,                       \
                        cpu_regs[a->rd], val);                          \
        tcg_temp_free(mem);                                             \
        return true;                                                    \
    }

#define FCONVOP(name, op)                                       \
    static bool trans_##name(DisasContext *ctx, arg_##name * a) \
    {                                                           \
        TCGv val, mem;                                          \
        mem = tcg_temp_new();                                   \
        val = rx_load_source(ctx, mem, a->ld, MO_32, a->rs);    \
        gen_helper_##op(cpu_regs[a->rd], cpu_env, val);         \
        tcg_temp_free(mem);                                     \
        return true;                                            \
    }

FOP(FADD, fadd)
FOP(FSUB, fsub)
FOP(FMUL, fmul)
FOP(FDIV, fdiv)

/* fcmp #imm, rd */
static bool trans_FCMP_ir(DisasContext *ctx, arg_FCMP_ir * a)
{
    TCGv imm = tcg_const_i32(li(ctx, 0));
    gen_helper_fcmp(cpu_env, cpu_regs[a->rd], imm);
    tcg_temp_free(imm);
    return true;
}

/* fcmp dsp[rs], rd */
/* fcmp rs, rd */
static bool trans_FCMP_mr(DisasContext *ctx, arg_FCMP_mr *a)
{
    TCGv val, mem;
    mem = tcg_temp_new();
    val = rx_load_source(ctx, mem, a->ld, MO_32, a->rs);
    gen_helper_fcmp(cpu_env, cpu_regs[a->rd], val);
    tcg_temp_free(mem);
    return true;
}

FCONVOP(FTOI, ftoi)
FCONVOP(ROUND, round)

/* itof rs, rd */
/* itof dsp[rs], rd */
static bool trans_ITOF(DisasContext *ctx, arg_ITOF * a)
{
    TCGv val, mem;
    mem = tcg_temp_new();
    val = rx_load_source(ctx, mem, a->ld, a->mi, a->rs);
    gen_helper_itof(cpu_regs[a->rd], cpu_env, val);
    tcg_temp_free(mem);
    return true;
}

static inline void rx_bsetm(TCGv mem, TCGv mask)
{
    TCGv val;
    val = tcg_temp_new();
    rx_gen_ld(MO_8, val, mem);
    tcg_gen_or_i32(val, val, mask);
    rx_gen_st(MO_8, val, mem);
    tcg_temp_free(val);
}

static inline void rx_bclrm(TCGv mem, TCGv mask)
{
    TCGv val;
    val = tcg_temp_new();
    rx_gen_ld(MO_8, val, mem);
    tcg_gen_not_i32(mask, mask);
    tcg_gen_and_i32(val, val, mask);
    rx_gen_st(MO_8, val, mem);
    tcg_temp_free(val);
}

static inline void rx_btstm(TCGv mem, TCGv mask)
{
    TCGv val;
    val = tcg_temp_new();
    rx_gen_ld(MO_8, val, mem);
    tcg_gen_and_i32(val, val, mask);
    tcg_gen_setcondi_i32(TCG_COND_NE, cpu_psw_c, val, 0);
    tcg_gen_mov_i32(cpu_psw_z, cpu_psw_c);
    tcg_temp_free(val);
}

static inline void rx_bnotm(TCGv mem, TCGv mask)
{
    TCGv val;
    val = tcg_temp_new();
    rx_gen_ld(MO_8, val, mem);
    tcg_gen_xor_i32(val, val, mask);
    rx_gen_st(MO_8, val, mem);
    tcg_temp_free(val);
}

static inline void rx_bsetr(TCGv reg, TCGv mask)
{
    tcg_gen_or_i32(reg, reg, mask);
}

static inline void rx_bclrr(TCGv reg, TCGv mask)
{
    tcg_gen_not_i32(mask, mask);
    tcg_gen_and_i32(reg, reg, mask);
}

static inline void rx_btstr(TCGv reg, TCGv mask)
{
    TCGv t0;
    t0 = tcg_temp_new();
    tcg_gen_and_i32(t0, reg, mask);
    tcg_gen_setcondi_i32(TCG_COND_NE, cpu_psw_c, t0, 0);
    tcg_gen_mov_i32(cpu_psw_z, cpu_psw_c);
    tcg_temp_free(t0);
}

static inline void rx_bnotr(TCGv reg, TCGv mask)
{
    tcg_gen_xor_i32(reg, reg, mask);
}

#define BITOP(name, op)                                                 \
    static bool cat3(trans_, name, _im)(DisasContext *ctx,              \
                                        cat3(arg_, name, _im) * a)      \
    {                                                                   \
        TCGv mask, mem, addr;                                           \
        mem = tcg_temp_new();                                           \
        mask = tcg_const_i32(1 << a->imm);                              \
        addr = rx_index_addr(ctx, mem, a->ld, MO_8, a->rs);             \
        cat3(rx_, op, m)(addr, mask);                                   \
        tcg_temp_free(mask);                                            \
        tcg_temp_free(mem);                                             \
        return true;                                                    \
    }                                                                   \
    static bool cat3(trans_, name, _ir)(DisasContext *ctx,              \
                                        cat3(arg_, name, _ir) * a)      \
    {                                                                   \
        TCGv mask;                                                      \
        mask = tcg_const_i32(1 << a->imm);                              \
        cat3(rx_, op, r)(cpu_regs[a->rd], mask);                        \
        tcg_temp_free(mask);                                            \
        return true;                                                    \
    }                                                                   \
    static bool cat3(trans_, name, _rr)(DisasContext *ctx,              \
                                        cat3(arg_, name, _rr) * a)      \
    {                                                                   \
        TCGv mask;                                                      \
        mask = tcg_const_i32(1);                                        \
        tcg_gen_shl_i32(mask, mask, cpu_regs[a->rs]);                   \
        cat3(rx_, op, r)(cpu_regs[a->rd], mask);                        \
        tcg_temp_free(mask);                                            \
        return true;                                                    \
    }                                                                   \
    static bool cat3(trans_, name, _rm)(DisasContext *ctx,              \
                                        cat3(arg_, name, _rm) * a)      \
    {                                                                   \
        TCGv mask, mem, addr;                                           \
        mask = tcg_const_i32(1);                                        \
        tcg_gen_shl_i32(mask, mask, cpu_regs[a->rd]);                   \
        mem = tcg_temp_new();                                           \
        addr = rx_index_addr(ctx, mem, a->ld, MO_8, a->rs);             \
        cat3(rx_, op, m)(addr, mask);                                   \
        tcg_temp_free(mem);                                             \
        tcg_temp_free(mask);                                            \
        return true;                                                    \
    }

BITOP(BSET, bset)
BITOP(BCLR, bclr)
BITOP(BTST, btst)
BITOP(BNOT, bnot)

static inline void bmcnd_op(TCGv val, TCGCond cond, int pos)
{
    TCGv bit;
    DisasCompare dc;
    dc.temp = tcg_temp_new();
    bit = tcg_temp_new();
    psw_cond(&dc, cond);
    tcg_gen_andi_i32(val, val, ~(1 << pos));
    tcg_gen_setcondi_i32(dc.cond, bit, dc.value, 0);
    tcg_gen_shli_i32(bit, bit, pos);
    tcg_gen_or_i32(val, val, bit);
    tcg_temp_free(bit);
    tcg_temp_free(dc.temp);
 }

/* bmcnd #imm, dsp[rd] */
static bool trans_BMCnd_im(DisasContext *ctx, arg_BMCnd_im *a)
{
    TCGv val, mem, addr;
    val = tcg_temp_new();
    mem = tcg_temp_new();
    addr = rx_index_addr(ctx, mem, a->ld, MO_8, a->rd);
    rx_gen_ld(MO_8, val, addr);
    bmcnd_op(val, a->cd, a->imm);
    rx_gen_st(MO_8, val, addr);
    tcg_temp_free(val);
    tcg_temp_free(mem);
    return true;
}

/* bmcond #imm, rd */
static bool trans_BMCnd_ir(DisasContext *ctx, arg_BMCnd_ir *a)
{
    bmcnd_op(cpu_regs[a->rd], a->cd, a->imm);
    return true;
}

static inline void clrsetpsw(DisasContext *ctx, int cb, int val)
{
    if (cb < 8) {
        switch (cb) {
        case PSW_C:
            tcg_gen_movi_i32(cpu_psw_c, val);
            break;
        case PSW_Z:
            tcg_gen_movi_i32(cpu_psw_z, val == 0);
            break;
        case PSW_S:
            tcg_gen_movi_i32(cpu_psw_s, val ? -1 : 0);
            break;
        case PSW_O:
            tcg_gen_movi_i32(cpu_psw_o, val << 31);
            break;
        }
    } else if (is_privileged(ctx, 0)) {
        switch (cb) {
        case PSW_I - 8:
            tcg_gen_movi_i32(cpu_psw_i, val);
            ctx->base.is_jmp = DISAS_UPDATE;
            break;
        case PSW_U - 8:
            tcg_gen_movi_i32(cpu_psw_u, val);
            break;
        }
    }
}

/* clrpsw psw */
static bool trans_CLRPSW(DisasContext *ctx, arg_CLRPSW *a)
{
    clrsetpsw(ctx, a->cb, 0);
    return true;
}

/* setpsw psw */
static bool trans_SETPSW(DisasContext *ctx, arg_SETPSW *a)
{
    clrsetpsw(ctx, a->cb, 1);
    return true;
}

/* mvtipl #imm */
static bool trans_MVTIPL(DisasContext *ctx, arg_MVTIPL *a)
{
    if (is_privileged(ctx, 1)) {
        tcg_gen_movi_i32(cpu_psw_ipl, a->imm);
        ctx->base.is_jmp = DISAS_UPDATE;
    }
    return true;
}

/* mvtc #imm, rd */
static bool trans_MVTC_i(DisasContext *ctx, arg_MVTC_i *a)
{
    TCGv cr, imm;

    imm = tcg_const_i32(a->imm);
    cr = tcg_const_i32(a->cr);
    if (allow_write_cr(ctx, a->cr)) {
        gen_helper_mvtc(cpu_env, cr, imm);
        if (a->cr == 0 && is_privileged(ctx, 0)) {
            ctx->base.is_jmp = DISAS_UPDATE;
        }
    }
    tcg_temp_free(cr);
    tcg_temp_free(imm);
    return true;
}

/* mvtc rs, rd */
static bool trans_MVTC_r(DisasContext *ctx, arg_MVTC_r *a)
{
    TCGv cr;

    cr = tcg_const_i32(a->cr);
    if (allow_write_cr(ctx, a->cr)) {
        gen_helper_mvtc(cpu_env, cr, cpu_regs[a->rs]);
        if (a->cr == 0 && is_privileged(ctx, 0)) {
            ctx->base.is_jmp = DISAS_UPDATE;
        }
    }
    tcg_temp_free(cr);
    return true;
}

/* mvfc rs, rd */
static bool trans_MVFC(DisasContext *ctx, arg_MVFC *a)
{
    TCGv cr;

    cr = tcg_const_i32(a->cr);
    if (a->cr == 1) {
        tcg_gen_movi_i32(cpu_regs[a->rd], ctx->pc);
    } else {
        gen_helper_mvfc(cpu_regs[a->rd], cpu_env, cr);
    }
    tcg_temp_free(cr);
    return true;
}

/* rtfi */
static bool trans_RTFI(DisasContext *ctx, arg_RTFI *a)
{
    if (is_privileged(ctx, 1)) {
        tcg_gen_mov_i32(cpu_pc, cpu_bpc);
        tcg_gen_mov_i32(cpu_psw, cpu_bpsw);
        gen_helper_unpack_psw(cpu_env);
        ctx->base.is_jmp = DISAS_EXIT;
    }
    return true;
}

/* rte */
static bool trans_RTE(DisasContext *ctx, arg_RTE *a)
{
    if (is_privileged(ctx, 1)) {
        rx_gen_ld(MO_32, cpu_pc, cpu_regs[0]);
        tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], 4);
        rx_gen_ld(MO_32, cpu_psw, cpu_regs[0]);
        tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], 4);
        gen_helper_unpack_psw(cpu_env);
        ctx->base.is_jmp = DISAS_EXIT;
    }
    return true;
}

/* brk */
static bool trans_BRK(DisasContext *ctx, arg_BRK *a)
{
    tcg_gen_movi_i32(cpu_pc, ctx->base.pc_next);
    gen_helper_rxbrk(cpu_env);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

/* int #imm */
static bool trans_INT(DisasContext *ctx, arg_INT *a)
{
    TCGv vec;

    tcg_debug_assert(a->imm < 0x100);
    vec = tcg_const_i32(a->imm);
    tcg_gen_movi_i32(cpu_pc, ctx->base.pc_next);
    gen_helper_rxint(cpu_env, vec);
    tcg_temp_free(vec);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

/* wait */
static bool trans_WAIT(DisasContext *ctx, arg_WAIT *a)
{
    if (is_privileged(ctx, 1)) {
        tcg_gen_addi_i32(cpu_pc, cpu_pc, 2);
        gen_helper_wait(cpu_env);
    }
    return true;
}

static void rx_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
}

static void rx_tr_tb_start(DisasContextBase *dcbase, CPUState *cs)
{
}

static void rx_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(ctx->base.pc_next);
}

static bool rx_tr_breakpoint_check(DisasContextBase *dcbase, CPUState *cs,
                                    const CPUBreakpoint *bp)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    /* We have hit a breakpoint - make sure PC is up-to-date */
    tcg_gen_movi_i32(cpu_pc, ctx->base.pc_next);
    gen_helper_debug(cpu_env);
    ctx->base.is_jmp = DISAS_NORETURN;
    ctx->base.pc_next += 1;
    return true;
}

static void rx_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    CPURXState *env = cs->env_ptr;
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    uint32_t insn;

    ctx->env = env;
    ctx->pc = ctx->base.pc_next;
    insn = decode_load(ctx);
    if (!decode(ctx, insn)) {
        gen_helper_raise_illegal_instruction(cpu_env);
    }
}

static void rx_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    switch (ctx->base.is_jmp) {
    case DISAS_NEXT:
    case DISAS_TOO_MANY:
        gen_goto_tb(ctx, 0, dcbase->pc_next);
        break;
    case DISAS_JUMP:
        if (ctx->base.singlestep_enabled) {
            gen_helper_debug(cpu_env);
        } else {
            tcg_gen_lookup_and_goto_ptr();
        }
        break;
    case DISAS_UPDATE:
        tcg_gen_movi_i32(cpu_pc, ctx->base.pc_next);
    case DISAS_EXIT:
        tcg_gen_exit_tb(NULL, 0);
        break;
    case DISAS_NORETURN:
        break;
    default:
        g_assert_not_reached();
    }
}

static void rx_tr_disas_log(const DisasContextBase *dcbase, CPUState *cs)
{
    qemu_log("IN:\n");  /* , lookup_symbol(dcbase->pc_first)); */
    log_target_disas(cs, dcbase->pc_first, dcbase->tb->size);
}

static const TranslatorOps rx_tr_ops = {
    .init_disas_context = rx_tr_init_disas_context,
    .tb_start           = rx_tr_tb_start,
    .insn_start         = rx_tr_insn_start,
    .breakpoint_check   = rx_tr_breakpoint_check,
    .translate_insn     = rx_tr_translate_insn,
    .tb_stop            = rx_tr_tb_stop,
    .disas_log          = rx_tr_disas_log,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb)
{
    DisasContext dc;

    translator_loop(&rx_tr_ops, &dc.base, cs, tb);
}

void restore_state_to_opc(CPURXState *env, TranslationBlock *tb,
                          target_ulong *data)
{
    env->pc = data[0];
    env->psw = data[1];
    rx_cpu_unpack_psw(env, 1);
}

#define ALLOC_REGISTER(sym, name) \
    cpu_##sym = tcg_global_mem_new_i32(cpu_env, \
                                       offsetof(CPURXState, sym), name)

void rx_translate_init(void)
{
    static const char * const regnames[16] = {
        "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7",
        "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15"
    };
    int i;

    for (i = 0; i < 16; i++) {
        cpu_regs[i] = tcg_global_mem_new_i32(cpu_env,
                                              offsetof(CPURXState, regs[i]),
                                              regnames[i]);
    }
    ALLOC_REGISTER(pc, "PC");
    ALLOC_REGISTER(psw, "PSW");
    ALLOC_REGISTER(psw_o, "PSW(O)");
    ALLOC_REGISTER(psw_s, "PSW(S)");
    ALLOC_REGISTER(psw_z, "PSW(Z)");
    ALLOC_REGISTER(psw_c, "PSW(C)");
    ALLOC_REGISTER(psw_u, "PSW(U)");
    ALLOC_REGISTER(psw_i, "PSW(I)");
    ALLOC_REGISTER(psw_pm, "PSW(PM)");
    ALLOC_REGISTER(psw_ipl, "PSW(IPL)");
    ALLOC_REGISTER(usp, "USP");
    ALLOC_REGISTER(fpsw, "FPSW");
    ALLOC_REGISTER(bpsw, "BPSW");
    ALLOC_REGISTER(bpc, "BPC");
    ALLOC_REGISTER(isp, "ISP");
    ALLOC_REGISTER(fintv, "FINTV");
    ALLOC_REGISTER(intb, "INTB");
    cpu_acc = tcg_global_mem_new_i64(cpu_env,
                                     offsetof(CPURXState, acc), "ACC");
}
