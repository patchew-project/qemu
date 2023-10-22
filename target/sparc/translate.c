/*
   SPARC translation

   Copyright (C) 2003 Thomas M. Ogrisegg <tom@fnord.at>
   Copyright (C) 2003-2005 Fabrice Bellard

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "cpu.h"
#include "disas/disas.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "tcg/tcg-op.h"

#include "exec/helper-gen.h"

#include "exec/translator.h"
#include "exec/log.h"
#include "asi.h"

#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#undef  HELPER_H

#ifdef TARGET_SPARC64
# define gen_helper_rdpsr(D, E)                 qemu_build_not_reached()
# define gen_helper_rett(E)                     qemu_build_not_reached()
# define gen_helper_power_down(E)               qemu_build_not_reached()
# define gen_helper_wrpsr(E, S)                 qemu_build_not_reached()
#else
# define gen_helper_clear_softint(E, S)         qemu_build_not_reached()
# define gen_helper_done(E)                     qemu_build_not_reached()
# define gen_helper_flushw(E)                   qemu_build_not_reached()
# define gen_helper_rdccr(D, E)                 qemu_build_not_reached()
# define gen_helper_rdcwp(D, E)                 qemu_build_not_reached()
# define gen_helper_restored(E)                 qemu_build_not_reached()
# define gen_helper_retry(E)                    qemu_build_not_reached()
# define gen_helper_saved(E)                    qemu_build_not_reached()
# define gen_helper_sdivx(D, E, A, B)           qemu_build_not_reached()
# define gen_helper_set_softint(E, S)           qemu_build_not_reached()
# define gen_helper_tick_get_count(D, E, T, C)  qemu_build_not_reached()
# define gen_helper_tick_set_count(P, S)        qemu_build_not_reached()
# define gen_helper_tick_set_limit(P, S)        qemu_build_not_reached()
# define gen_helper_udivx(D, E, A, B)           qemu_build_not_reached()
# define gen_helper_wrccr(E, S)                 qemu_build_not_reached()
# define gen_helper_wrcwp(E, S)                 qemu_build_not_reached()
# define gen_helper_wrgl(E, S)                  qemu_build_not_reached()
# define gen_helper_write_softint(E, S)         qemu_build_not_reached()
# define gen_helper_wrpil(E, S)                 qemu_build_not_reached()
# define gen_helper_wrpstate(E, S)              qemu_build_not_reached()
# define MAXTL_MASK                             0
#endif

/* Dynamic PC, must exit to main loop. */
#define DYNAMIC_PC         1
/* Dynamic PC, one of two values according to jump_pc[T2]. */
#define JUMP_PC            2
/* Dynamic PC, may lookup next TB. */
#define DYNAMIC_PC_LOOKUP  3

#define DISAS_EXIT  DISAS_TARGET_0

/* global register indexes */
static TCGv_ptr cpu_regwptr;
static TCGv cpu_cc_src, cpu_cc_src2, cpu_cc_dst;
static TCGv_i32 cpu_cc_op;
static TCGv_i32 cpu_psr;
static TCGv cpu_fsr, cpu_pc, cpu_npc;
static TCGv cpu_regs[32];
static TCGv cpu_y;
static TCGv cpu_tbr;
static TCGv cpu_cond;
#ifdef TARGET_SPARC64
static TCGv_i32 cpu_xcc, cpu_fprs;
static TCGv cpu_gsr;
#else
# define cpu_fprs               ({ qemu_build_not_reached(); (TCGv)NULL; })
# define cpu_gsr                ({ qemu_build_not_reached(); (TCGv)NULL; })
#endif
/* Floating point registers */
static TCGv_i64 cpu_fpr[TARGET_DPREGS];

#define env_field_offsetof(X)     offsetof(CPUSPARCState, X)
#ifdef TARGET_SPARC64
# define env32_field_offsetof(X)  ({ qemu_build_not_reached(); 0; })
# define env64_field_offsetof(X)  env_field_offsetof(X)
#else
# define env32_field_offsetof(X)  env_field_offsetof(X)
# define env64_field_offsetof(X)  ({ qemu_build_not_reached(); 0; })
#endif

typedef struct DisasDelayException {
    struct DisasDelayException *next;
    TCGLabel *lab;
    TCGv_i32 excp;
    /* Saved state at parent insn. */
    target_ulong pc;
    target_ulong npc;
} DisasDelayException;

typedef struct DisasContext {
    DisasContextBase base;
    target_ulong pc;    /* current Program Counter: integer or DYNAMIC_PC */
    target_ulong npc;   /* next PC: integer or DYNAMIC_PC or JUMP_PC */
    target_ulong jump_pc[2]; /* used when JUMP_PC pc value is used */
    int mem_idx;
    bool fpu_enabled;
    bool address_mask_32bit;
#ifndef CONFIG_USER_ONLY
    bool supervisor;
#ifdef TARGET_SPARC64
    bool hypervisor;
#endif
#endif

    uint32_t cc_op;  /* current CC operation */
    sparc_def_t *def;
#ifdef TARGET_SPARC64
    int fprs_dirty;
    int asi;
#endif
    DisasDelayException *delay_excp_list;
} DisasContext;

typedef struct {
    TCGCond cond;
    bool is_bool;
    TCGv c1, c2;
} DisasCompare;

// This function uses non-native bit order
#define GET_FIELD(X, FROM, TO)                                  \
    ((X) >> (31 - (TO)) & ((1 << ((TO) - (FROM) + 1)) - 1))

// This function uses the order in the manuals, i.e. bit 0 is 2^0
#define GET_FIELD_SP(X, FROM, TO)               \
    GET_FIELD(X, 31 - (TO), 31 - (FROM))

#define GET_FIELDs(x,a,b) sign_extend (GET_FIELD(x,a,b), (b) - (a) + 1)
#define GET_FIELD_SPs(x,a,b) sign_extend (GET_FIELD_SP(x,a,b), ((b) - (a) + 1))

#ifdef TARGET_SPARC64
#define DFPREG(r) (((r & 1) << 5) | (r & 0x1e))
#define QFPREG(r) (((r & 1) << 5) | (r & 0x1c))
#else
#define DFPREG(r) (r & 0x1e)
#define QFPREG(r) (r & 0x1c)
#endif

#define UA2005_HTRAP_MASK 0xff
#define V8_TRAP_MASK 0x7f

static int sign_extend(int x, int len)
{
    len = 32 - len;
    return (x << len) >> len;
}

#define IS_IMM (insn & (1<<13))

static void gen_update_fprs_dirty(DisasContext *dc, int rd)
{
#if defined(TARGET_SPARC64)
    int bit = (rd < 32) ? 1 : 2;
    /* If we know we've already set this bit within the TB,
       we can avoid setting it again.  */
    if (!(dc->fprs_dirty & bit)) {
        dc->fprs_dirty |= bit;
        tcg_gen_ori_i32(cpu_fprs, cpu_fprs, bit);
    }
#endif
}

/* floating point registers moves */
static TCGv_i32 gen_load_fpr_F(DisasContext *dc, unsigned int src)
{
    TCGv_i32 ret = tcg_temp_new_i32();
    if (src & 1) {
        tcg_gen_extrl_i64_i32(ret, cpu_fpr[src / 2]);
    } else {
        tcg_gen_extrh_i64_i32(ret, cpu_fpr[src / 2]);
    }
    return ret;
}

static void gen_store_fpr_F(DisasContext *dc, unsigned int dst, TCGv_i32 v)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_extu_i32_i64(t, v);
    tcg_gen_deposit_i64(cpu_fpr[dst / 2], cpu_fpr[dst / 2], t,
                        (dst & 1 ? 0 : 32), 32);
    gen_update_fprs_dirty(dc, dst);
}

static TCGv_i32 gen_dest_fpr_F(DisasContext *dc)
{
    return tcg_temp_new_i32();
}

static TCGv_i64 gen_load_fpr_D(DisasContext *dc, unsigned int src)
{
    src = DFPREG(src);
    return cpu_fpr[src / 2];
}

static void gen_store_fpr_D(DisasContext *dc, unsigned int dst, TCGv_i64 v)
{
    dst = DFPREG(dst);
    tcg_gen_mov_i64(cpu_fpr[dst / 2], v);
    gen_update_fprs_dirty(dc, dst);
}

static TCGv_i64 gen_dest_fpr_D(DisasContext *dc, unsigned int dst)
{
    return cpu_fpr[DFPREG(dst) / 2];
}

static void gen_op_load_fpr_QT0(unsigned int src)
{
    tcg_gen_st_i64(cpu_fpr[src / 2], tcg_env, offsetof(CPUSPARCState, qt0) +
                   offsetof(CPU_QuadU, ll.upper));
    tcg_gen_st_i64(cpu_fpr[src/2 + 1], tcg_env, offsetof(CPUSPARCState, qt0) +
                   offsetof(CPU_QuadU, ll.lower));
}

static void gen_op_load_fpr_QT1(unsigned int src)
{
    tcg_gen_st_i64(cpu_fpr[src / 2], tcg_env, offsetof(CPUSPARCState, qt1) +
                   offsetof(CPU_QuadU, ll.upper));
    tcg_gen_st_i64(cpu_fpr[src/2 + 1], tcg_env, offsetof(CPUSPARCState, qt1) +
                   offsetof(CPU_QuadU, ll.lower));
}

static void gen_op_store_QT0_fpr(unsigned int dst)
{
    tcg_gen_ld_i64(cpu_fpr[dst / 2], tcg_env, offsetof(CPUSPARCState, qt0) +
                   offsetof(CPU_QuadU, ll.upper));
    tcg_gen_ld_i64(cpu_fpr[dst/2 + 1], tcg_env, offsetof(CPUSPARCState, qt0) +
                   offsetof(CPU_QuadU, ll.lower));
}

static void gen_store_fpr_Q(DisasContext *dc, unsigned int dst,
                            TCGv_i64 v1, TCGv_i64 v2)
{
    dst = QFPREG(dst);

    tcg_gen_mov_i64(cpu_fpr[dst / 2], v1);
    tcg_gen_mov_i64(cpu_fpr[dst / 2 + 1], v2);
    gen_update_fprs_dirty(dc, dst);
}

#ifdef TARGET_SPARC64
static TCGv_i64 gen_load_fpr_Q0(DisasContext *dc, unsigned int src)
{
    src = QFPREG(src);
    return cpu_fpr[src / 2];
}

static TCGv_i64 gen_load_fpr_Q1(DisasContext *dc, unsigned int src)
{
    src = QFPREG(src);
    return cpu_fpr[src / 2 + 1];
}

static void gen_move_Q(DisasContext *dc, unsigned int rd, unsigned int rs)
{
    rd = QFPREG(rd);
    rs = QFPREG(rs);

    tcg_gen_mov_i64(cpu_fpr[rd / 2], cpu_fpr[rs / 2]);
    tcg_gen_mov_i64(cpu_fpr[rd / 2 + 1], cpu_fpr[rs / 2 + 1]);
    gen_update_fprs_dirty(dc, rd);
}
#endif

/* moves */
#ifdef CONFIG_USER_ONLY
#define supervisor(dc) 0
#define hypervisor(dc) 0
#else
#ifdef TARGET_SPARC64
#define hypervisor(dc) (dc->hypervisor)
#define supervisor(dc) (dc->supervisor | dc->hypervisor)
#else
#define supervisor(dc) (dc->supervisor)
#define hypervisor(dc) 0
#endif
#endif

#if !defined(TARGET_SPARC64)
# define AM_CHECK(dc)  false
#elif defined(TARGET_ABI32)
# define AM_CHECK(dc)  true
#elif defined(CONFIG_USER_ONLY)
# define AM_CHECK(dc)  false
#else
# define AM_CHECK(dc)  ((dc)->address_mask_32bit)
#endif

static void gen_address_mask(DisasContext *dc, TCGv addr)
{
    if (AM_CHECK(dc)) {
        tcg_gen_andi_tl(addr, addr, 0xffffffffULL);
    }
}

static target_ulong address_mask_i(DisasContext *dc, target_ulong addr)
{
    return AM_CHECK(dc) ? (uint32_t)addr : addr;
}

static TCGv gen_load_gpr(DisasContext *dc, int reg)
{
    if (reg > 0) {
        assert(reg < 32);
        return cpu_regs[reg];
    } else {
        TCGv t = tcg_temp_new();
        tcg_gen_movi_tl(t, 0);
        return t;
    }
}

static void gen_store_gpr(DisasContext *dc, int reg, TCGv v)
{
    if (reg > 0) {
        assert(reg < 32);
        tcg_gen_mov_tl(cpu_regs[reg], v);
    }
}

static TCGv gen_dest_gpr(DisasContext *dc, int reg)
{
    if (reg > 0) {
        assert(reg < 32);
        return cpu_regs[reg];
    } else {
        return tcg_temp_new();
    }
}

static bool use_goto_tb(DisasContext *s, target_ulong pc, target_ulong npc)
{
    return translator_use_goto_tb(&s->base, pc) &&
           translator_use_goto_tb(&s->base, npc);
}

static void gen_goto_tb(DisasContext *s, int tb_num,
                        target_ulong pc, target_ulong npc)
{
    if (use_goto_tb(s, pc, npc))  {
        /* jump to same page: we can use a direct jump */
        tcg_gen_goto_tb(tb_num);
        tcg_gen_movi_tl(cpu_pc, pc);
        tcg_gen_movi_tl(cpu_npc, npc);
        tcg_gen_exit_tb(s->base.tb, tb_num);
    } else {
        /* jump to another page: we can use an indirect jump */
        tcg_gen_movi_tl(cpu_pc, pc);
        tcg_gen_movi_tl(cpu_npc, npc);
        tcg_gen_lookup_and_goto_ptr();
    }
}

// XXX suboptimal
static void gen_mov_reg_N(TCGv reg, TCGv_i32 src)
{
    tcg_gen_extu_i32_tl(reg, src);
    tcg_gen_extract_tl(reg, reg, PSR_NEG_SHIFT, 1);
}

static void gen_mov_reg_Z(TCGv reg, TCGv_i32 src)
{
    tcg_gen_extu_i32_tl(reg, src);
    tcg_gen_extract_tl(reg, reg, PSR_ZERO_SHIFT, 1);
}

static void gen_mov_reg_V(TCGv reg, TCGv_i32 src)
{
    tcg_gen_extu_i32_tl(reg, src);
    tcg_gen_extract_tl(reg, reg, PSR_OVF_SHIFT, 1);
}

static void gen_mov_reg_C(TCGv reg, TCGv_i32 src)
{
    tcg_gen_extu_i32_tl(reg, src);
    tcg_gen_extract_tl(reg, reg, PSR_CARRY_SHIFT, 1);
}

static void gen_op_add_cc(TCGv dst, TCGv src1, TCGv src2)
{
    tcg_gen_mov_tl(cpu_cc_src, src1);
    tcg_gen_mov_tl(cpu_cc_src2, src2);
    tcg_gen_add_tl(cpu_cc_dst, cpu_cc_src, cpu_cc_src2);
    tcg_gen_mov_tl(dst, cpu_cc_dst);
}

static TCGv_i32 gen_add32_carry32(void)
{
    TCGv_i32 carry_32, cc_src1_32, cc_src2_32;

    /* Carry is computed from a previous add: (dst < src)  */
#if TARGET_LONG_BITS == 64
    cc_src1_32 = tcg_temp_new_i32();
    cc_src2_32 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(cc_src1_32, cpu_cc_dst);
    tcg_gen_extrl_i64_i32(cc_src2_32, cpu_cc_src);
#else
    cc_src1_32 = cpu_cc_dst;
    cc_src2_32 = cpu_cc_src;
#endif

    carry_32 = tcg_temp_new_i32();
    tcg_gen_setcond_i32(TCG_COND_LTU, carry_32, cc_src1_32, cc_src2_32);

    return carry_32;
}

static TCGv_i32 gen_sub32_carry32(void)
{
    TCGv_i32 carry_32, cc_src1_32, cc_src2_32;

    /* Carry is computed from a previous borrow: (src1 < src2)  */
#if TARGET_LONG_BITS == 64
    cc_src1_32 = tcg_temp_new_i32();
    cc_src2_32 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(cc_src1_32, cpu_cc_src);
    tcg_gen_extrl_i64_i32(cc_src2_32, cpu_cc_src2);
#else
    cc_src1_32 = cpu_cc_src;
    cc_src2_32 = cpu_cc_src2;
#endif

    carry_32 = tcg_temp_new_i32();
    tcg_gen_setcond_i32(TCG_COND_LTU, carry_32, cc_src1_32, cc_src2_32);

    return carry_32;
}

static void gen_op_addc_int(TCGv dst, TCGv src1, TCGv src2,
                            TCGv_i32 carry_32, bool update_cc)
{
    tcg_gen_add_tl(dst, src1, src2);

#ifdef TARGET_SPARC64
    TCGv carry = tcg_temp_new();
    tcg_gen_extu_i32_tl(carry, carry_32);
    tcg_gen_add_tl(dst, dst, carry);
#else
    tcg_gen_add_i32(dst, dst, carry_32);
#endif

    if (update_cc) {
        tcg_debug_assert(dst == cpu_cc_dst);
        tcg_gen_mov_tl(cpu_cc_src, src1);
        tcg_gen_mov_tl(cpu_cc_src2, src2);
    }
}

static void gen_op_addc_int_add(TCGv dst, TCGv src1, TCGv src2, bool update_cc)
{
    TCGv discard;

    if (TARGET_LONG_BITS == 64) {
        gen_op_addc_int(dst, src1, src2, gen_add32_carry32(), update_cc);
        return;
    }

    /*
     * We can re-use the host's hardware carry generation by using
     * an ADD2 opcode.  We discard the low part of the output.
     * Ideally we'd combine this operation with the add that
     * generated the carry in the first place.
     */
    discard = tcg_temp_new();
    tcg_gen_add2_tl(discard, dst, cpu_cc_src, src1, cpu_cc_src2, src2);

    if (update_cc) {
        tcg_debug_assert(dst == cpu_cc_dst);
        tcg_gen_mov_tl(cpu_cc_src, src1);
        tcg_gen_mov_tl(cpu_cc_src2, src2);
    }
}

static void gen_op_addc_add(TCGv dst, TCGv src1, TCGv src2)
{
    gen_op_addc_int_add(dst, src1, src2, false);
}

static void gen_op_addccc_add(TCGv dst, TCGv src1, TCGv src2)
{
    gen_op_addc_int_add(dst, src1, src2, true);
}

static void gen_op_addc_sub(TCGv dst, TCGv src1, TCGv src2)
{
    gen_op_addc_int(dst, src1, src2, gen_sub32_carry32(), false);
}

static void gen_op_addccc_sub(TCGv dst, TCGv src1, TCGv src2)
{
    gen_op_addc_int(dst, src1, src2, gen_sub32_carry32(), true);
}

static void gen_op_addc_int_generic(TCGv dst, TCGv src1, TCGv src2,
                                    bool update_cc)
{
    TCGv_i32 carry_32 = tcg_temp_new_i32();
    gen_helper_compute_C_icc(carry_32, tcg_env);
    gen_op_addc_int(dst, src1, src2, carry_32, update_cc);
}

static void gen_op_addc_generic(TCGv dst, TCGv src1, TCGv src2)
{
    gen_op_addc_int_generic(dst, src1, src2, false);
}

static void gen_op_addccc_generic(TCGv dst, TCGv src1, TCGv src2)
{
    gen_op_addc_int_generic(dst, src1, src2, true);
}

static void gen_op_sub_cc(TCGv dst, TCGv src1, TCGv src2)
{
    tcg_gen_mov_tl(cpu_cc_src, src1);
    tcg_gen_mov_tl(cpu_cc_src2, src2);
    tcg_gen_sub_tl(cpu_cc_dst, cpu_cc_src, cpu_cc_src2);
    tcg_gen_mov_tl(dst, cpu_cc_dst);
}

static void gen_op_subc_int(TCGv dst, TCGv src1, TCGv src2,
                            TCGv_i32 carry_32, bool update_cc)
{
    TCGv carry;

#if TARGET_LONG_BITS == 64
    carry = tcg_temp_new();
    tcg_gen_extu_i32_i64(carry, carry_32);
#else
    carry = carry_32;
#endif

    tcg_gen_sub_tl(dst, src1, src2);
    tcg_gen_sub_tl(dst, dst, carry);

    if (update_cc) {
        tcg_debug_assert(dst == cpu_cc_dst);
        tcg_gen_mov_tl(cpu_cc_src, src1);
        tcg_gen_mov_tl(cpu_cc_src2, src2);
    }
}

static void gen_op_subc_add(TCGv dst, TCGv src1, TCGv src2)
{
    gen_op_subc_int(dst, src1, src2, gen_add32_carry32(), false);
}

static void gen_op_subccc_add(TCGv dst, TCGv src1, TCGv src2)
{
    gen_op_subc_int(dst, src1, src2, gen_sub32_carry32(), true);
}

static void gen_op_subc_int_sub(TCGv dst, TCGv src1, TCGv src2, bool update_cc)
{
    TCGv discard;

    if (TARGET_LONG_BITS == 64) {
        gen_op_subc_int(dst, src1, src2, gen_sub32_carry32(), update_cc);
        return;
    }

    /*
     * We can re-use the host's hardware carry generation by using
     * a SUB2 opcode.  We discard the low part of the output.
     */
    discard = tcg_temp_new();
    tcg_gen_sub2_tl(discard, dst, cpu_cc_src, src1, cpu_cc_src2, src2);

    if (update_cc) {
        tcg_debug_assert(dst == cpu_cc_dst);
        tcg_gen_mov_tl(cpu_cc_src, src1);
        tcg_gen_mov_tl(cpu_cc_src2, src2);
    }
}

static void gen_op_subc_sub(TCGv dst, TCGv src1, TCGv src2)
{
    gen_op_subc_int_sub(dst, src1, src2, false);
}

static void gen_op_subccc_sub(TCGv dst, TCGv src1, TCGv src2)
{
    gen_op_subc_int_sub(dst, src1, src2, true);
}

static void gen_op_subc_int_generic(TCGv dst, TCGv src1, TCGv src2,
                                    bool update_cc)
{
    TCGv_i32 carry_32 = tcg_temp_new_i32();

    gen_helper_compute_C_icc(carry_32, tcg_env);
    gen_op_subc_int(dst, src1, src2, carry_32, update_cc);
}

static void gen_op_subc_generic(TCGv dst, TCGv src1, TCGv src2)
{
    gen_op_subc_int_generic(dst, src1, src2, false);
}

static void gen_op_subccc_generic(TCGv dst, TCGv src1, TCGv src2)
{
    gen_op_subc_int_generic(dst, src1, src2, true);
}

static void gen_op_mulscc(TCGv dst, TCGv src1, TCGv src2)
{
    TCGv r_temp, zero, t0;

    r_temp = tcg_temp_new();
    t0 = tcg_temp_new();

    /* old op:
    if (!(env->y & 1))
        T1 = 0;
    */
    zero = tcg_constant_tl(0);
    tcg_gen_andi_tl(cpu_cc_src, src1, 0xffffffff);
    tcg_gen_andi_tl(r_temp, cpu_y, 0x1);
    tcg_gen_andi_tl(cpu_cc_src2, src2, 0xffffffff);
    tcg_gen_movcond_tl(TCG_COND_EQ, cpu_cc_src2, r_temp, zero,
                       zero, cpu_cc_src2);

    // b2 = T0 & 1;
    // env->y = (b2 << 31) | (env->y >> 1);
    tcg_gen_extract_tl(t0, cpu_y, 1, 31);
    tcg_gen_deposit_tl(cpu_y, t0, cpu_cc_src, 31, 1);

    // b1 = N ^ V;
    gen_mov_reg_N(t0, cpu_psr);
    gen_mov_reg_V(r_temp, cpu_psr);
    tcg_gen_xor_tl(t0, t0, r_temp);

    // T0 = (b1 << 31) | (T0 >> 1);
    // src1 = T0;
    tcg_gen_shli_tl(t0, t0, 31);
    tcg_gen_shri_tl(cpu_cc_src, cpu_cc_src, 1);
    tcg_gen_or_tl(cpu_cc_src, cpu_cc_src, t0);

    tcg_gen_add_tl(cpu_cc_dst, cpu_cc_src, cpu_cc_src2);

    tcg_gen_mov_tl(dst, cpu_cc_dst);
}

static void gen_op_multiply(TCGv dst, TCGv src1, TCGv src2, int sign_ext)
{
#if TARGET_LONG_BITS == 32
    if (sign_ext) {
        tcg_gen_muls2_tl(dst, cpu_y, src1, src2);
    } else {
        tcg_gen_mulu2_tl(dst, cpu_y, src1, src2);
    }
#else
    TCGv t0 = tcg_temp_new_i64();
    TCGv t1 = tcg_temp_new_i64();

    if (sign_ext) {
        tcg_gen_ext32s_i64(t0, src1);
        tcg_gen_ext32s_i64(t1, src2);
    } else {
        tcg_gen_ext32u_i64(t0, src1);
        tcg_gen_ext32u_i64(t1, src2);
    }

    tcg_gen_mul_i64(dst, t0, t1);
    tcg_gen_shri_i64(cpu_y, dst, 32);
#endif
}

static void gen_op_umul(TCGv dst, TCGv src1, TCGv src2)
{
    /* zero-extend truncated operands before multiplication */
    gen_op_multiply(dst, src1, src2, 0);
}

static void gen_op_smul(TCGv dst, TCGv src1, TCGv src2)
{
    /* sign-extend truncated operands before multiplication */
    gen_op_multiply(dst, src1, src2, 1);
}

static void gen_op_udivx(TCGv dst, TCGv src1, TCGv src2)
{
    gen_helper_udivx(dst, tcg_env, src1, src2);
}

static void gen_op_sdivx(TCGv dst, TCGv src1, TCGv src2)
{
    gen_helper_sdivx(dst, tcg_env, src1, src2);
}

static void gen_op_udiv(TCGv dst, TCGv src1, TCGv src2)
{
    gen_helper_udiv(dst, tcg_env, src1, src2);
}

static void gen_op_sdiv(TCGv dst, TCGv src1, TCGv src2)
{
    gen_helper_sdiv(dst, tcg_env, src1, src2);
}

static void gen_op_udivcc(TCGv dst, TCGv src1, TCGv src2)
{
    gen_helper_udiv_cc(dst, tcg_env, src1, src2);
}

static void gen_op_sdivcc(TCGv dst, TCGv src1, TCGv src2)
{
    gen_helper_sdiv_cc(dst, tcg_env, src1, src2);
}

static void gen_op_taddcctv(TCGv dst, TCGv src1, TCGv src2)
{
    gen_helper_taddcctv(dst, tcg_env, src1, src2);
}

static void gen_op_tsubcctv(TCGv dst, TCGv src1, TCGv src2)
{
    gen_helper_tsubcctv(dst, tcg_env, src1, src2);
}

static void gen_op_popc(TCGv dst, TCGv src1, TCGv src2)
{
    tcg_gen_ctpop_tl(dst, src2);
}

// 1
static void gen_op_eval_ba(TCGv dst)
{
    tcg_gen_movi_tl(dst, 1);
}

// Z
static void gen_op_eval_be(TCGv dst, TCGv_i32 src)
{
    gen_mov_reg_Z(dst, src);
}

// Z | (N ^ V)
static void gen_op_eval_ble(TCGv dst, TCGv_i32 src)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_N(t0, src);
    gen_mov_reg_V(dst, src);
    tcg_gen_xor_tl(dst, dst, t0);
    gen_mov_reg_Z(t0, src);
    tcg_gen_or_tl(dst, dst, t0);
}

// N ^ V
static void gen_op_eval_bl(TCGv dst, TCGv_i32 src)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_V(t0, src);
    gen_mov_reg_N(dst, src);
    tcg_gen_xor_tl(dst, dst, t0);
}

// C | Z
static void gen_op_eval_bleu(TCGv dst, TCGv_i32 src)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_Z(t0, src);
    gen_mov_reg_C(dst, src);
    tcg_gen_or_tl(dst, dst, t0);
}

// C
static void gen_op_eval_bcs(TCGv dst, TCGv_i32 src)
{
    gen_mov_reg_C(dst, src);
}

// V
static void gen_op_eval_bvs(TCGv dst, TCGv_i32 src)
{
    gen_mov_reg_V(dst, src);
}

// 0
static void gen_op_eval_bn(TCGv dst)
{
    tcg_gen_movi_tl(dst, 0);
}

// N
static void gen_op_eval_bneg(TCGv dst, TCGv_i32 src)
{
    gen_mov_reg_N(dst, src);
}

// !Z
static void gen_op_eval_bne(TCGv dst, TCGv_i32 src)
{
    gen_mov_reg_Z(dst, src);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// !(Z | (N ^ V))
static void gen_op_eval_bg(TCGv dst, TCGv_i32 src)
{
    gen_op_eval_ble(dst, src);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// !(N ^ V)
static void gen_op_eval_bge(TCGv dst, TCGv_i32 src)
{
    gen_op_eval_bl(dst, src);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// !(C | Z)
static void gen_op_eval_bgu(TCGv dst, TCGv_i32 src)
{
    gen_op_eval_bleu(dst, src);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// !C
static void gen_op_eval_bcc(TCGv dst, TCGv_i32 src)
{
    gen_mov_reg_C(dst, src);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// !N
static void gen_op_eval_bpos(TCGv dst, TCGv_i32 src)
{
    gen_mov_reg_N(dst, src);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// !V
static void gen_op_eval_bvc(TCGv dst, TCGv_i32 src)
{
    gen_mov_reg_V(dst, src);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

/*
  FPSR bit field FCC1 | FCC0:
   0 =
   1 <
   2 >
   3 unordered
*/
static void gen_mov_reg_FCC0(TCGv reg, TCGv src,
                                    unsigned int fcc_offset)
{
    tcg_gen_shri_tl(reg, src, FSR_FCC0_SHIFT + fcc_offset);
    tcg_gen_andi_tl(reg, reg, 0x1);
}

static void gen_mov_reg_FCC1(TCGv reg, TCGv src, unsigned int fcc_offset)
{
    tcg_gen_shri_tl(reg, src, FSR_FCC1_SHIFT + fcc_offset);
    tcg_gen_andi_tl(reg, reg, 0x1);
}

// !0: FCC0 | FCC1
static void gen_op_eval_fbne(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_or_tl(dst, dst, t0);
}

// 1 or 2: FCC0 ^ FCC1
static void gen_op_eval_fblg(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_xor_tl(dst, dst, t0);
}

// 1 or 3: FCC0
static void gen_op_eval_fbul(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    gen_mov_reg_FCC0(dst, src, fcc_offset);
}

// 1: FCC0 & !FCC1
static void gen_op_eval_fbl(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_andc_tl(dst, dst, t0);
}

// 2 or 3: FCC1
static void gen_op_eval_fbug(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    gen_mov_reg_FCC1(dst, src, fcc_offset);
}

// 2: !FCC0 & FCC1
static void gen_op_eval_fbg(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_andc_tl(dst, t0, dst);
}

// 3: FCC0 & FCC1
static void gen_op_eval_fbu(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_and_tl(dst, dst, t0);
}

// 0: !(FCC0 | FCC1)
static void gen_op_eval_fbe(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_or_tl(dst, dst, t0);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// 0 or 3: !(FCC0 ^ FCC1)
static void gen_op_eval_fbue(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_xor_tl(dst, dst, t0);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// 0 or 2: !FCC0
static void gen_op_eval_fbge(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// !1: !(FCC0 & !FCC1)
static void gen_op_eval_fbuge(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_andc_tl(dst, dst, t0);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// 0 or 1: !FCC1
static void gen_op_eval_fble(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    gen_mov_reg_FCC1(dst, src, fcc_offset);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// !2: !(!FCC0 & FCC1)
static void gen_op_eval_fbule(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_andc_tl(dst, t0, dst);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// !3: !(FCC0 & FCC1)
static void gen_op_eval_fbo(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_and_tl(dst, dst, t0);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

static void gen_branch2(DisasContext *dc, target_ulong pc1,
                        target_ulong pc2, TCGv r_cond)
{
    TCGLabel *l1 = gen_new_label();

    tcg_gen_brcondi_tl(TCG_COND_EQ, r_cond, 0, l1);

    gen_goto_tb(dc, 0, pc1, pc1 + 4);

    gen_set_label(l1);
    gen_goto_tb(dc, 1, pc2, pc2 + 4);
}

static void gen_generic_branch(DisasContext *dc)
{
    TCGv npc0 = tcg_constant_tl(dc->jump_pc[0]);
    TCGv npc1 = tcg_constant_tl(dc->jump_pc[1]);
    TCGv zero = tcg_constant_tl(0);

    tcg_gen_movcond_tl(TCG_COND_NE, cpu_npc, cpu_cond, zero, npc0, npc1);
}

/* call this function before using the condition register as it may
   have been set for a jump */
static void flush_cond(DisasContext *dc)
{
    if (dc->npc == JUMP_PC) {
        gen_generic_branch(dc);
        dc->npc = DYNAMIC_PC_LOOKUP;
    }
}

static void save_npc(DisasContext *dc)
{
    if (dc->npc & 3) {
        switch (dc->npc) {
        case JUMP_PC:
            gen_generic_branch(dc);
            dc->npc = DYNAMIC_PC_LOOKUP;
            break;
        case DYNAMIC_PC:
        case DYNAMIC_PC_LOOKUP:
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        tcg_gen_movi_tl(cpu_npc, dc->npc);
    }
}

static void update_psr(DisasContext *dc)
{
    if (dc->cc_op != CC_OP_FLAGS) {
        dc->cc_op = CC_OP_FLAGS;
        gen_helper_compute_psr(tcg_env);
    }
}

static void save_state(DisasContext *dc)
{
    tcg_gen_movi_tl(cpu_pc, dc->pc);
    save_npc(dc);
}

static void gen_exception(DisasContext *dc, int which)
{
    save_state(dc);
    gen_helper_raise_exception(tcg_env, tcg_constant_i32(which));
    dc->base.is_jmp = DISAS_NORETURN;
}

static TCGLabel *delay_exceptionv(DisasContext *dc, TCGv_i32 excp)
{
    DisasDelayException *e = g_new0(DisasDelayException, 1);

    e->next = dc->delay_excp_list;
    dc->delay_excp_list = e;

    e->lab = gen_new_label();
    e->excp = excp;
    e->pc = dc->pc;
    /* Caller must have used flush_cond before branch. */
    assert(e->npc != JUMP_PC);
    e->npc = dc->npc;

    return e->lab;
}

static TCGLabel *delay_exception(DisasContext *dc, int excp)
{
    return delay_exceptionv(dc, tcg_constant_i32(excp));
}

static void gen_check_align(DisasContext *dc, TCGv addr, int mask)
{
    TCGv t = tcg_temp_new();
    TCGLabel *lab;

    tcg_gen_andi_tl(t, addr, mask);

    flush_cond(dc);
    lab = delay_exception(dc, TT_UNALIGNED);
    tcg_gen_brcondi_tl(TCG_COND_NE, t, 0, lab);
}

static void gen_mov_pc_npc(DisasContext *dc)
{
    if (dc->npc & 3) {
        switch (dc->npc) {
        case JUMP_PC:
            gen_generic_branch(dc);
            tcg_gen_mov_tl(cpu_pc, cpu_npc);
            dc->pc = DYNAMIC_PC_LOOKUP;
            break;
        case DYNAMIC_PC:
        case DYNAMIC_PC_LOOKUP:
            tcg_gen_mov_tl(cpu_pc, cpu_npc);
            dc->pc = dc->npc;
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        dc->pc = dc->npc;
    }
}

static void gen_op_next_insn(void)
{
    tcg_gen_mov_tl(cpu_pc, cpu_npc);
    tcg_gen_addi_tl(cpu_npc, cpu_npc, 4);
}

static void gen_compare(DisasCompare *cmp, bool xcc, unsigned int cond,
                        DisasContext *dc)
{
    static int subcc_cond[16] = {
        TCG_COND_NEVER,
        TCG_COND_EQ,
        TCG_COND_LE,
        TCG_COND_LT,
        TCG_COND_LEU,
        TCG_COND_LTU,
        -1, /* neg */
        -1, /* overflow */
        TCG_COND_ALWAYS,
        TCG_COND_NE,
        TCG_COND_GT,
        TCG_COND_GE,
        TCG_COND_GTU,
        TCG_COND_GEU,
        -1, /* pos */
        -1, /* no overflow */
    };

    static int logic_cond[16] = {
        TCG_COND_NEVER,
        TCG_COND_EQ,     /* eq:  Z */
        TCG_COND_LE,     /* le:  Z | (N ^ V) -> Z | N */
        TCG_COND_LT,     /* lt:  N ^ V -> N */
        TCG_COND_EQ,     /* leu: C | Z -> Z */
        TCG_COND_NEVER,  /* ltu: C -> 0 */
        TCG_COND_LT,     /* neg: N */
        TCG_COND_NEVER,  /* vs:  V -> 0 */
        TCG_COND_ALWAYS,
        TCG_COND_NE,     /* ne:  !Z */
        TCG_COND_GT,     /* gt:  !(Z | (N ^ V)) -> !(Z | N) */
        TCG_COND_GE,     /* ge:  !(N ^ V) -> !N */
        TCG_COND_NE,     /* gtu: !(C | Z) -> !Z */
        TCG_COND_ALWAYS, /* geu: !C -> 1 */
        TCG_COND_GE,     /* pos: !N */
        TCG_COND_ALWAYS, /* vc:  !V -> 1 */
    };

    TCGv_i32 r_src;
    TCGv r_dst;

#ifdef TARGET_SPARC64
    if (xcc) {
        r_src = cpu_xcc;
    } else {
        r_src = cpu_psr;
    }
#else
    r_src = cpu_psr;
#endif

    switch (dc->cc_op) {
    case CC_OP_LOGIC:
        cmp->cond = logic_cond[cond];
    do_compare_dst_0:
        cmp->is_bool = false;
        cmp->c2 = tcg_constant_tl(0);
#ifdef TARGET_SPARC64
        if (!xcc) {
            cmp->c1 = tcg_temp_new();
            tcg_gen_ext32s_tl(cmp->c1, cpu_cc_dst);
            break;
        }
#endif
        cmp->c1 = cpu_cc_dst;
        break;

    case CC_OP_SUB:
        switch (cond) {
        case 6:  /* neg */
        case 14: /* pos */
            cmp->cond = (cond == 6 ? TCG_COND_LT : TCG_COND_GE);
            goto do_compare_dst_0;

        case 7: /* overflow */
        case 15: /* !overflow */
            goto do_dynamic;

        default:
            cmp->cond = subcc_cond[cond];
            cmp->is_bool = false;
#ifdef TARGET_SPARC64
            if (!xcc) {
                /* Note that sign-extension works for unsigned compares as
                   long as both operands are sign-extended.  */
                cmp->c1 = tcg_temp_new();
                cmp->c2 = tcg_temp_new();
                tcg_gen_ext32s_tl(cmp->c1, cpu_cc_src);
                tcg_gen_ext32s_tl(cmp->c2, cpu_cc_src2);
                break;
            }
#endif
            cmp->c1 = cpu_cc_src;
            cmp->c2 = cpu_cc_src2;
            break;
        }
        break;

    default:
    do_dynamic:
        gen_helper_compute_psr(tcg_env);
        dc->cc_op = CC_OP_FLAGS;
        /* FALLTHRU */

    case CC_OP_FLAGS:
        /* We're going to generate a boolean result.  */
        cmp->cond = TCG_COND_NE;
        cmp->is_bool = true;
        cmp->c1 = r_dst = tcg_temp_new();
        cmp->c2 = tcg_constant_tl(0);

        switch (cond) {
        case 0x0:
            gen_op_eval_bn(r_dst);
            break;
        case 0x1:
            gen_op_eval_be(r_dst, r_src);
            break;
        case 0x2:
            gen_op_eval_ble(r_dst, r_src);
            break;
        case 0x3:
            gen_op_eval_bl(r_dst, r_src);
            break;
        case 0x4:
            gen_op_eval_bleu(r_dst, r_src);
            break;
        case 0x5:
            gen_op_eval_bcs(r_dst, r_src);
            break;
        case 0x6:
            gen_op_eval_bneg(r_dst, r_src);
            break;
        case 0x7:
            gen_op_eval_bvs(r_dst, r_src);
            break;
        case 0x8:
            gen_op_eval_ba(r_dst);
            break;
        case 0x9:
            gen_op_eval_bne(r_dst, r_src);
            break;
        case 0xa:
            gen_op_eval_bg(r_dst, r_src);
            break;
        case 0xb:
            gen_op_eval_bge(r_dst, r_src);
            break;
        case 0xc:
            gen_op_eval_bgu(r_dst, r_src);
            break;
        case 0xd:
            gen_op_eval_bcc(r_dst, r_src);
            break;
        case 0xe:
            gen_op_eval_bpos(r_dst, r_src);
            break;
        case 0xf:
            gen_op_eval_bvc(r_dst, r_src);
            break;
        }
        break;
    }
}

static void gen_fcompare(DisasCompare *cmp, unsigned int cc, unsigned int cond)
{
    unsigned int offset;
    TCGv r_dst;

    /* For now we still generate a straight boolean result.  */
    cmp->cond = TCG_COND_NE;
    cmp->is_bool = true;
    cmp->c1 = r_dst = tcg_temp_new();
    cmp->c2 = tcg_constant_tl(0);

    switch (cc) {
    default:
    case 0x0:
        offset = 0;
        break;
    case 0x1:
        offset = 32 - 10;
        break;
    case 0x2:
        offset = 34 - 10;
        break;
    case 0x3:
        offset = 36 - 10;
        break;
    }

    switch (cond) {
    case 0x0:
        gen_op_eval_bn(r_dst);
        break;
    case 0x1:
        gen_op_eval_fbne(r_dst, cpu_fsr, offset);
        break;
    case 0x2:
        gen_op_eval_fblg(r_dst, cpu_fsr, offset);
        break;
    case 0x3:
        gen_op_eval_fbul(r_dst, cpu_fsr, offset);
        break;
    case 0x4:
        gen_op_eval_fbl(r_dst, cpu_fsr, offset);
        break;
    case 0x5:
        gen_op_eval_fbug(r_dst, cpu_fsr, offset);
        break;
    case 0x6:
        gen_op_eval_fbg(r_dst, cpu_fsr, offset);
        break;
    case 0x7:
        gen_op_eval_fbu(r_dst, cpu_fsr, offset);
        break;
    case 0x8:
        gen_op_eval_ba(r_dst);
        break;
    case 0x9:
        gen_op_eval_fbe(r_dst, cpu_fsr, offset);
        break;
    case 0xa:
        gen_op_eval_fbue(r_dst, cpu_fsr, offset);
        break;
    case 0xb:
        gen_op_eval_fbge(r_dst, cpu_fsr, offset);
        break;
    case 0xc:
        gen_op_eval_fbuge(r_dst, cpu_fsr, offset);
        break;
    case 0xd:
        gen_op_eval_fble(r_dst, cpu_fsr, offset);
        break;
    case 0xe:
        gen_op_eval_fbule(r_dst, cpu_fsr, offset);
        break;
    case 0xf:
        gen_op_eval_fbo(r_dst, cpu_fsr, offset);
        break;
    }
}

// Inverted logic
static const TCGCond gen_tcg_cond_reg[8] = {
    TCG_COND_NEVER,  /* reserved */
    TCG_COND_NE,
    TCG_COND_GT,
    TCG_COND_GE,
    TCG_COND_NEVER,  /* reserved */
    TCG_COND_EQ,
    TCG_COND_LE,
    TCG_COND_LT,
};

static void gen_compare_reg(DisasCompare *cmp, int cond, TCGv r_src)
{
    cmp->cond = tcg_invert_cond(gen_tcg_cond_reg[cond]);
    cmp->is_bool = false;
    cmp->c1 = r_src;
    cmp->c2 = tcg_constant_tl(0);
}

#ifdef TARGET_SPARC64
static void gen_op_fcmps(int fccno, TCGv_i32 r_rs1, TCGv_i32 r_rs2)
{
    switch (fccno) {
    case 0:
        gen_helper_fcmps(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 1:
        gen_helper_fcmps_fcc1(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 2:
        gen_helper_fcmps_fcc2(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 3:
        gen_helper_fcmps_fcc3(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    }
}

static void gen_op_fcmpd(int fccno, TCGv_i64 r_rs1, TCGv_i64 r_rs2)
{
    switch (fccno) {
    case 0:
        gen_helper_fcmpd(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 1:
        gen_helper_fcmpd_fcc1(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 2:
        gen_helper_fcmpd_fcc2(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 3:
        gen_helper_fcmpd_fcc3(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    }
}

static void gen_op_fcmpq(int fccno)
{
    switch (fccno) {
    case 0:
        gen_helper_fcmpq(cpu_fsr, tcg_env);
        break;
    case 1:
        gen_helper_fcmpq_fcc1(cpu_fsr, tcg_env);
        break;
    case 2:
        gen_helper_fcmpq_fcc2(cpu_fsr, tcg_env);
        break;
    case 3:
        gen_helper_fcmpq_fcc3(cpu_fsr, tcg_env);
        break;
    }
}

static void gen_op_fcmpes(int fccno, TCGv_i32 r_rs1, TCGv_i32 r_rs2)
{
    switch (fccno) {
    case 0:
        gen_helper_fcmpes(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 1:
        gen_helper_fcmpes_fcc1(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 2:
        gen_helper_fcmpes_fcc2(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 3:
        gen_helper_fcmpes_fcc3(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    }
}

static void gen_op_fcmped(int fccno, TCGv_i64 r_rs1, TCGv_i64 r_rs2)
{
    switch (fccno) {
    case 0:
        gen_helper_fcmped(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 1:
        gen_helper_fcmped_fcc1(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 2:
        gen_helper_fcmped_fcc2(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 3:
        gen_helper_fcmped_fcc3(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    }
}

static void gen_op_fcmpeq(int fccno)
{
    switch (fccno) {
    case 0:
        gen_helper_fcmpeq(cpu_fsr, tcg_env);
        break;
    case 1:
        gen_helper_fcmpeq_fcc1(cpu_fsr, tcg_env);
        break;
    case 2:
        gen_helper_fcmpeq_fcc2(cpu_fsr, tcg_env);
        break;
    case 3:
        gen_helper_fcmpeq_fcc3(cpu_fsr, tcg_env);
        break;
    }
}

#else

static void gen_op_fcmps(int fccno, TCGv r_rs1, TCGv r_rs2)
{
    gen_helper_fcmps(cpu_fsr, tcg_env, r_rs1, r_rs2);
}

static void gen_op_fcmpd(int fccno, TCGv_i64 r_rs1, TCGv_i64 r_rs2)
{
    gen_helper_fcmpd(cpu_fsr, tcg_env, r_rs1, r_rs2);
}

static void gen_op_fcmpq(int fccno)
{
    gen_helper_fcmpq(cpu_fsr, tcg_env);
}

static void gen_op_fcmpes(int fccno, TCGv r_rs1, TCGv r_rs2)
{
    gen_helper_fcmpes(cpu_fsr, tcg_env, r_rs1, r_rs2);
}

static void gen_op_fcmped(int fccno, TCGv_i64 r_rs1, TCGv_i64 r_rs2)
{
    gen_helper_fcmped(cpu_fsr, tcg_env, r_rs1, r_rs2);
}

static void gen_op_fcmpeq(int fccno)
{
    gen_helper_fcmpeq(cpu_fsr, tcg_env);
}
#endif

static void gen_op_fpexception_im(DisasContext *dc, int fsr_flags)
{
    tcg_gen_andi_tl(cpu_fsr, cpu_fsr, FSR_FTT_NMASK);
    tcg_gen_ori_tl(cpu_fsr, cpu_fsr, fsr_flags);
    gen_exception(dc, TT_FP_EXCP);
}

static int gen_trap_ifnofpu(DisasContext *dc)
{
#if !defined(CONFIG_USER_ONLY)
    if (!dc->fpu_enabled) {
        gen_exception(dc, TT_NFPU_INSN);
        return 1;
    }
#endif
    return 0;
}

static void gen_op_clear_ieee_excp_and_FTT(void)
{
    tcg_gen_andi_tl(cpu_fsr, cpu_fsr, FSR_FTT_CEXC_NMASK);
}

static void gen_fop_FF(DisasContext *dc, int rd, int rs,
                              void (*gen)(TCGv_i32, TCGv_ptr, TCGv_i32))
{
    TCGv_i32 dst, src;

    src = gen_load_fpr_F(dc, rs);
    dst = gen_dest_fpr_F(dc);

    gen(dst, tcg_env, src);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);

    gen_store_fpr_F(dc, rd, dst);
}

static void gen_ne_fop_FF(DisasContext *dc, int rd, int rs,
                          void (*gen)(TCGv_i32, TCGv_i32))
{
    TCGv_i32 dst, src;

    src = gen_load_fpr_F(dc, rs);
    dst = gen_dest_fpr_F(dc);

    gen(dst, src);

    gen_store_fpr_F(dc, rd, dst);
}

static void gen_fop_FFF(DisasContext *dc, int rd, int rs1, int rs2,
                        void (*gen)(TCGv_i32, TCGv_ptr, TCGv_i32, TCGv_i32))
{
    TCGv_i32 dst, src1, src2;

    src1 = gen_load_fpr_F(dc, rs1);
    src2 = gen_load_fpr_F(dc, rs2);
    dst = gen_dest_fpr_F(dc);

    gen(dst, tcg_env, src1, src2);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);

    gen_store_fpr_F(dc, rd, dst);
}

#ifdef TARGET_SPARC64
static void gen_ne_fop_FFF(DisasContext *dc, int rd, int rs1, int rs2,
                           void (*gen)(TCGv_i32, TCGv_i32, TCGv_i32))
{
    TCGv_i32 dst, src1, src2;

    src1 = gen_load_fpr_F(dc, rs1);
    src2 = gen_load_fpr_F(dc, rs2);
    dst = gen_dest_fpr_F(dc);

    gen(dst, src1, src2);

    gen_store_fpr_F(dc, rd, dst);
}
#endif

static void gen_fop_DD(DisasContext *dc, int rd, int rs,
                       void (*gen)(TCGv_i64, TCGv_ptr, TCGv_i64))
{
    TCGv_i64 dst, src;

    src = gen_load_fpr_D(dc, rs);
    dst = gen_dest_fpr_D(dc, rd);

    gen(dst, tcg_env, src);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);

    gen_store_fpr_D(dc, rd, dst);
}

#ifdef TARGET_SPARC64
static void gen_ne_fop_DD(DisasContext *dc, int rd, int rs,
                          void (*gen)(TCGv_i64, TCGv_i64))
{
    TCGv_i64 dst, src;

    src = gen_load_fpr_D(dc, rs);
    dst = gen_dest_fpr_D(dc, rd);

    gen(dst, src);

    gen_store_fpr_D(dc, rd, dst);
}
#endif

static void gen_fop_DDD(DisasContext *dc, int rd, int rs1, int rs2,
                        void (*gen)(TCGv_i64, TCGv_ptr, TCGv_i64, TCGv_i64))
{
    TCGv_i64 dst, src1, src2;

    src1 = gen_load_fpr_D(dc, rs1);
    src2 = gen_load_fpr_D(dc, rs2);
    dst = gen_dest_fpr_D(dc, rd);

    gen(dst, tcg_env, src1, src2);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);

    gen_store_fpr_D(dc, rd, dst);
}

#ifdef TARGET_SPARC64
static void gen_ne_fop_DDD(DisasContext *dc, int rd, int rs1, int rs2,
                           void (*gen)(TCGv_i64, TCGv_i64, TCGv_i64))
{
    TCGv_i64 dst, src1, src2;

    src1 = gen_load_fpr_D(dc, rs1);
    src2 = gen_load_fpr_D(dc, rs2);
    dst = gen_dest_fpr_D(dc, rd);

    gen(dst, src1, src2);

    gen_store_fpr_D(dc, rd, dst);
}

static void gen_gsr_fop_DDD(DisasContext *dc, int rd, int rs1, int rs2,
                            void (*gen)(TCGv_i64, TCGv_i64, TCGv_i64, TCGv_i64))
{
    TCGv_i64 dst, src1, src2;

    src1 = gen_load_fpr_D(dc, rs1);
    src2 = gen_load_fpr_D(dc, rs2);
    dst = gen_dest_fpr_D(dc, rd);

    gen(dst, cpu_gsr, src1, src2);

    gen_store_fpr_D(dc, rd, dst);
}

static void gen_ne_fop_DDDD(DisasContext *dc, int rd, int rs1, int rs2,
                            void (*gen)(TCGv_i64, TCGv_i64, TCGv_i64, TCGv_i64))
{
    TCGv_i64 dst, src0, src1, src2;

    src1 = gen_load_fpr_D(dc, rs1);
    src2 = gen_load_fpr_D(dc, rs2);
    src0 = gen_load_fpr_D(dc, rd);
    dst = gen_dest_fpr_D(dc, rd);

    gen(dst, src0, src1, src2);

    gen_store_fpr_D(dc, rd, dst);
}
#endif

static void gen_fop_QQ(DisasContext *dc, int rd, int rs,
                       void (*gen)(TCGv_ptr))
{
    gen_op_load_fpr_QT1(QFPREG(rs));

    gen(tcg_env);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);

    gen_op_store_QT0_fpr(QFPREG(rd));
    gen_update_fprs_dirty(dc, QFPREG(rd));
}

#ifdef TARGET_SPARC64
static void gen_ne_fop_QQ(DisasContext *dc, int rd, int rs,
                          void (*gen)(TCGv_ptr))
{
    gen_op_load_fpr_QT1(QFPREG(rs));

    gen(tcg_env);

    gen_op_store_QT0_fpr(QFPREG(rd));
    gen_update_fprs_dirty(dc, QFPREG(rd));
}
#endif

static void gen_fop_QQQ(DisasContext *dc, int rd, int rs1, int rs2,
                        void (*gen)(TCGv_ptr))
{
    gen_op_load_fpr_QT0(QFPREG(rs1));
    gen_op_load_fpr_QT1(QFPREG(rs2));

    gen(tcg_env);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);

    gen_op_store_QT0_fpr(QFPREG(rd));
    gen_update_fprs_dirty(dc, QFPREG(rd));
}

static void gen_fop_DFF(DisasContext *dc, int rd, int rs1, int rs2,
                        void (*gen)(TCGv_i64, TCGv_ptr, TCGv_i32, TCGv_i32))
{
    TCGv_i64 dst;
    TCGv_i32 src1, src2;

    src1 = gen_load_fpr_F(dc, rs1);
    src2 = gen_load_fpr_F(dc, rs2);
    dst = gen_dest_fpr_D(dc, rd);

    gen(dst, tcg_env, src1, src2);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);

    gen_store_fpr_D(dc, rd, dst);
}

static void gen_fop_QDD(DisasContext *dc, int rd, int rs1, int rs2,
                        void (*gen)(TCGv_ptr, TCGv_i64, TCGv_i64))
{
    TCGv_i64 src1, src2;

    src1 = gen_load_fpr_D(dc, rs1);
    src2 = gen_load_fpr_D(dc, rs2);

    gen(tcg_env, src1, src2);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);

    gen_op_store_QT0_fpr(QFPREG(rd));
    gen_update_fprs_dirty(dc, QFPREG(rd));
}

#ifdef TARGET_SPARC64
static void gen_fop_DF(DisasContext *dc, int rd, int rs,
                       void (*gen)(TCGv_i64, TCGv_ptr, TCGv_i32))
{
    TCGv_i64 dst;
    TCGv_i32 src;

    src = gen_load_fpr_F(dc, rs);
    dst = gen_dest_fpr_D(dc, rd);

    gen(dst, tcg_env, src);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);

    gen_store_fpr_D(dc, rd, dst);
}
#endif

static void gen_ne_fop_DF(DisasContext *dc, int rd, int rs,
                          void (*gen)(TCGv_i64, TCGv_ptr, TCGv_i32))
{
    TCGv_i64 dst;
    TCGv_i32 src;

    src = gen_load_fpr_F(dc, rs);
    dst = gen_dest_fpr_D(dc, rd);

    gen(dst, tcg_env, src);

    gen_store_fpr_D(dc, rd, dst);
}

static void gen_fop_FD(DisasContext *dc, int rd, int rs,
                       void (*gen)(TCGv_i32, TCGv_ptr, TCGv_i64))
{
    TCGv_i32 dst;
    TCGv_i64 src;

    src = gen_load_fpr_D(dc, rs);
    dst = gen_dest_fpr_F(dc);

    gen(dst, tcg_env, src);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);

    gen_store_fpr_F(dc, rd, dst);
}

static void gen_fop_FQ(DisasContext *dc, int rd, int rs,
                       void (*gen)(TCGv_i32, TCGv_ptr))
{
    TCGv_i32 dst;

    gen_op_load_fpr_QT1(QFPREG(rs));
    dst = gen_dest_fpr_F(dc);

    gen(dst, tcg_env);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);

    gen_store_fpr_F(dc, rd, dst);
}

static void gen_fop_DQ(DisasContext *dc, int rd, int rs,
                       void (*gen)(TCGv_i64, TCGv_ptr))
{
    TCGv_i64 dst;

    gen_op_load_fpr_QT1(QFPREG(rs));
    dst = gen_dest_fpr_D(dc, rd);

    gen(dst, tcg_env);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);

    gen_store_fpr_D(dc, rd, dst);
}

static void gen_ne_fop_QF(DisasContext *dc, int rd, int rs,
                          void (*gen)(TCGv_ptr, TCGv_i32))
{
    TCGv_i32 src;

    src = gen_load_fpr_F(dc, rs);

    gen(tcg_env, src);

    gen_op_store_QT0_fpr(QFPREG(rd));
    gen_update_fprs_dirty(dc, QFPREG(rd));
}

static void gen_ne_fop_QD(DisasContext *dc, int rd, int rs,
                          void (*gen)(TCGv_ptr, TCGv_i64))
{
    TCGv_i64 src;

    src = gen_load_fpr_D(dc, rs);

    gen(tcg_env, src);

    gen_op_store_QT0_fpr(QFPREG(rd));
    gen_update_fprs_dirty(dc, QFPREG(rd));
}

static void gen_swap(DisasContext *dc, TCGv dst, TCGv src,
                     TCGv addr, int mmu_idx, MemOp memop)
{
    gen_address_mask(dc, addr);
    tcg_gen_atomic_xchg_tl(dst, addr, src, mmu_idx, memop | MO_ALIGN);
}

static void gen_ldstub(DisasContext *dc, TCGv dst, TCGv addr, int mmu_idx)
{
    TCGv m1 = tcg_constant_tl(0xff);
    gen_address_mask(dc, addr);
    tcg_gen_atomic_xchg_tl(dst, addr, m1, mmu_idx, MO_UB);
}

/* asi moves */
typedef enum {
    GET_ASI_HELPER,
    GET_ASI_EXCP,
    GET_ASI_DIRECT,
    GET_ASI_DTWINX,
    GET_ASI_BLOCK,
    GET_ASI_SHORT,
    GET_ASI_BCOPY,
    GET_ASI_BFILL,
} ASIType;

typedef struct {
    ASIType type;
    int asi;
    int mem_idx;
    MemOp memop;
} DisasASI;

/*
 * Build DisasASI.
 * For asi == -1, treat as non-asi.
 * For ask == -2, treat as immediate offset (v8 error, v9 %asi).
 */
static DisasASI resolve_asi(DisasContext *dc, int asi, MemOp memop)
{
    ASIType type = GET_ASI_HELPER;
    int mem_idx = dc->mem_idx;

    if (asi == -1) {
        /* Artificial "non-asi" case. */
        type = GET_ASI_DIRECT;
        goto done;
    }

#ifndef TARGET_SPARC64
    /* Before v9, all asis are immediate and privileged.  */
    if (asi < 0) {
        gen_exception(dc, TT_ILL_INSN);
        type = GET_ASI_EXCP;
    } else if (supervisor(dc)
               /* Note that LEON accepts ASI_USERDATA in user mode, for
                  use with CASA.  Also note that previous versions of
                  QEMU allowed (and old versions of gcc emitted) ASI_P
                  for LEON, which is incorrect.  */
               || (asi == ASI_USERDATA
                   && (dc->def->features & CPU_FEATURE_CASA))) {
        switch (asi) {
        case ASI_USERDATA:   /* User data access */
            mem_idx = MMU_USER_IDX;
            type = GET_ASI_DIRECT;
            break;
        case ASI_KERNELDATA: /* Supervisor data access */
            mem_idx = MMU_KERNEL_IDX;
            type = GET_ASI_DIRECT;
            break;
        case ASI_M_BYPASS:    /* MMU passthrough */
        case ASI_LEON_BYPASS: /* LEON MMU passthrough */
            mem_idx = MMU_PHYS_IDX;
            type = GET_ASI_DIRECT;
            break;
        case ASI_M_BCOPY: /* Block copy, sta access */
            mem_idx = MMU_KERNEL_IDX;
            type = GET_ASI_BCOPY;
            break;
        case ASI_M_BFILL: /* Block fill, stda access */
            mem_idx = MMU_KERNEL_IDX;
            type = GET_ASI_BFILL;
            break;
        }

        /* MMU_PHYS_IDX is used when the MMU is disabled to passthrough the
         * permissions check in get_physical_address(..).
         */
        mem_idx = (dc->mem_idx == MMU_PHYS_IDX) ? MMU_PHYS_IDX : mem_idx;
    } else {
        gen_exception(dc, TT_PRIV_INSN);
        type = GET_ASI_EXCP;
    }
#else
    if (asi < 0) {
        asi = dc->asi;
    }
    /* With v9, all asis below 0x80 are privileged.  */
    /* ??? We ought to check cpu_has_hypervisor, but we didn't copy
       down that bit into DisasContext.  For the moment that's ok,
       since the direct implementations below doesn't have any ASIs
       in the restricted [0x30, 0x7f] range, and the check will be
       done properly in the helper.  */
    if (!supervisor(dc) && asi < 0x80) {
        gen_exception(dc, TT_PRIV_ACT);
        type = GET_ASI_EXCP;
    } else {
        switch (asi) {
        case ASI_REAL:      /* Bypass */
        case ASI_REAL_IO:   /* Bypass, non-cacheable */
        case ASI_REAL_L:    /* Bypass LE */
        case ASI_REAL_IO_L: /* Bypass, non-cacheable LE */
        case ASI_TWINX_REAL:   /* Real address, twinx */
        case ASI_TWINX_REAL_L: /* Real address, twinx, LE */
        case ASI_QUAD_LDD_PHYS:
        case ASI_QUAD_LDD_PHYS_L:
            mem_idx = MMU_PHYS_IDX;
            break;
        case ASI_N:  /* Nucleus */
        case ASI_NL: /* Nucleus LE */
        case ASI_TWINX_N:
        case ASI_TWINX_NL:
        case ASI_NUCLEUS_QUAD_LDD:
        case ASI_NUCLEUS_QUAD_LDD_L:
            if (hypervisor(dc)) {
                mem_idx = MMU_PHYS_IDX;
            } else {
                mem_idx = MMU_NUCLEUS_IDX;
            }
            break;
        case ASI_AIUP:  /* As if user primary */
        case ASI_AIUPL: /* As if user primary LE */
        case ASI_TWINX_AIUP:
        case ASI_TWINX_AIUP_L:
        case ASI_BLK_AIUP_4V:
        case ASI_BLK_AIUP_L_4V:
        case ASI_BLK_AIUP:
        case ASI_BLK_AIUPL:
            mem_idx = MMU_USER_IDX;
            break;
        case ASI_AIUS:  /* As if user secondary */
        case ASI_AIUSL: /* As if user secondary LE */
        case ASI_TWINX_AIUS:
        case ASI_TWINX_AIUS_L:
        case ASI_BLK_AIUS_4V:
        case ASI_BLK_AIUS_L_4V:
        case ASI_BLK_AIUS:
        case ASI_BLK_AIUSL:
            mem_idx = MMU_USER_SECONDARY_IDX;
            break;
        case ASI_S:  /* Secondary */
        case ASI_SL: /* Secondary LE */
        case ASI_TWINX_S:
        case ASI_TWINX_SL:
        case ASI_BLK_COMMIT_S:
        case ASI_BLK_S:
        case ASI_BLK_SL:
        case ASI_FL8_S:
        case ASI_FL8_SL:
        case ASI_FL16_S:
        case ASI_FL16_SL:
            if (mem_idx == MMU_USER_IDX) {
                mem_idx = MMU_USER_SECONDARY_IDX;
            } else if (mem_idx == MMU_KERNEL_IDX) {
                mem_idx = MMU_KERNEL_SECONDARY_IDX;
            }
            break;
        case ASI_P:  /* Primary */
        case ASI_PL: /* Primary LE */
        case ASI_TWINX_P:
        case ASI_TWINX_PL:
        case ASI_BLK_COMMIT_P:
        case ASI_BLK_P:
        case ASI_BLK_PL:
        case ASI_FL8_P:
        case ASI_FL8_PL:
        case ASI_FL16_P:
        case ASI_FL16_PL:
            break;
        }
        switch (asi) {
        case ASI_REAL:
        case ASI_REAL_IO:
        case ASI_REAL_L:
        case ASI_REAL_IO_L:
        case ASI_N:
        case ASI_NL:
        case ASI_AIUP:
        case ASI_AIUPL:
        case ASI_AIUS:
        case ASI_AIUSL:
        case ASI_S:
        case ASI_SL:
        case ASI_P:
        case ASI_PL:
            type = GET_ASI_DIRECT;
            break;
        case ASI_TWINX_REAL:
        case ASI_TWINX_REAL_L:
        case ASI_TWINX_N:
        case ASI_TWINX_NL:
        case ASI_TWINX_AIUP:
        case ASI_TWINX_AIUP_L:
        case ASI_TWINX_AIUS:
        case ASI_TWINX_AIUS_L:
        case ASI_TWINX_P:
        case ASI_TWINX_PL:
        case ASI_TWINX_S:
        case ASI_TWINX_SL:
        case ASI_QUAD_LDD_PHYS:
        case ASI_QUAD_LDD_PHYS_L:
        case ASI_NUCLEUS_QUAD_LDD:
        case ASI_NUCLEUS_QUAD_LDD_L:
            type = GET_ASI_DTWINX;
            break;
        case ASI_BLK_COMMIT_P:
        case ASI_BLK_COMMIT_S:
        case ASI_BLK_AIUP_4V:
        case ASI_BLK_AIUP_L_4V:
        case ASI_BLK_AIUP:
        case ASI_BLK_AIUPL:
        case ASI_BLK_AIUS_4V:
        case ASI_BLK_AIUS_L_4V:
        case ASI_BLK_AIUS:
        case ASI_BLK_AIUSL:
        case ASI_BLK_S:
        case ASI_BLK_SL:
        case ASI_BLK_P:
        case ASI_BLK_PL:
            type = GET_ASI_BLOCK;
            break;
        case ASI_FL8_S:
        case ASI_FL8_SL:
        case ASI_FL8_P:
        case ASI_FL8_PL:
            memop = MO_UB;
            type = GET_ASI_SHORT;
            break;
        case ASI_FL16_S:
        case ASI_FL16_SL:
        case ASI_FL16_P:
        case ASI_FL16_PL:
            memop = MO_TEUW;
            type = GET_ASI_SHORT;
            break;
        }
        /* The little-endian asis all have bit 3 set.  */
        if (asi & 8) {
            memop ^= MO_BSWAP;
        }
    }
#endif

 done:
    return (DisasASI){ type, asi, mem_idx, memop };
}

static DisasASI get_asi(DisasContext *dc, int insn, MemOp memop)
{
    int asi = IS_IMM ? -2 : GET_FIELD(insn, 19, 26);
    return resolve_asi(dc, asi, memop);
}

#if defined(CONFIG_USER_ONLY) && !defined(TARGET_SPARC64)
static void gen_helper_ld_asi(TCGv_i64 r, TCGv_env e, TCGv a,
                              TCGv_i32 asi, TCGv_i32 mop)
{
    g_assert_not_reached();
}

static void gen_helper_st_asi(TCGv_env e, TCGv a, TCGv_i64 r,
                              TCGv_i32 asi, TCGv_i32 mop)
{
    g_assert_not_reached();
}
#endif

static void gen_ld_asi0(DisasContext *dc, DisasASI *da, TCGv dst, TCGv addr)
{
    switch (da->type) {
    case GET_ASI_EXCP:
        break;
    case GET_ASI_DTWINX: /* Reserved for ldda.  */
        gen_exception(dc, TT_ILL_INSN);
        break;
    case GET_ASI_DIRECT:
        tcg_gen_qemu_ld_tl(dst, addr, da->mem_idx, da->memop | MO_ALIGN);
        break;
    default:
        {
            TCGv_i32 r_asi = tcg_constant_i32(da->asi);
            TCGv_i32 r_mop = tcg_constant_i32(da->memop | MO_ALIGN);

            save_state(dc);
#ifdef TARGET_SPARC64
            gen_helper_ld_asi(dst, tcg_env, addr, r_asi, r_mop);
#else
            {
                TCGv_i64 t64 = tcg_temp_new_i64();
                gen_helper_ld_asi(t64, tcg_env, addr, r_asi, r_mop);
                tcg_gen_trunc_i64_tl(dst, t64);
            }
#endif
        }
        break;
    }
}

static void __attribute__((unused))
gen_ld_asi(DisasContext *dc, TCGv dst, TCGv addr, int insn, MemOp memop)
{
    DisasASI da = get_asi(dc, insn, memop);

    gen_address_mask(dc, addr);
    gen_ld_asi0(dc, &da, dst, addr);
}

static void gen_st_asi0(DisasContext *dc, DisasASI *da, TCGv src, TCGv addr)
{
    switch (da->type) {
    case GET_ASI_EXCP:
        break;

    case GET_ASI_DTWINX: /* Reserved for stda.  */
        if (TARGET_LONG_BITS == 32) {
            gen_exception(dc, TT_ILL_INSN);
            break;
        } else if (!(dc->def->features & CPU_FEATURE_HYPV)) {
            /* Pre OpenSPARC CPUs don't have these */
            gen_exception(dc, TT_ILL_INSN);
            break;
        }
        /* In OpenSPARC T1+ CPUs TWINX ASIs in store are ST_BLKINIT_ ASIs */
        /* fall through */

    case GET_ASI_DIRECT:
        tcg_gen_qemu_st_tl(src, addr, da->mem_idx, da->memop | MO_ALIGN);
        break;

    case GET_ASI_BCOPY:
        assert(TARGET_LONG_BITS == 32);
        /* Copy 32 bytes from the address in SRC to ADDR.  */
        /* ??? The original qemu code suggests 4-byte alignment, dropping
           the low bits, but the only place I can see this used is in the
           Linux kernel with 32 byte alignment, which would make more sense
           as a cacheline-style operation.  */
        {
            TCGv saddr = tcg_temp_new();
            TCGv daddr = tcg_temp_new();
            TCGv four = tcg_constant_tl(4);
            TCGv_i32 tmp = tcg_temp_new_i32();
            int i;

            tcg_gen_andi_tl(saddr, src, -4);
            tcg_gen_andi_tl(daddr, addr, -4);
            for (i = 0; i < 32; i += 4) {
                /* Since the loads and stores are paired, allow the
                   copy to happen in the host endianness.  */
                tcg_gen_qemu_ld_i32(tmp, saddr, da->mem_idx, MO_UL);
                tcg_gen_qemu_st_i32(tmp, daddr, da->mem_idx, MO_UL);
                tcg_gen_add_tl(saddr, saddr, four);
                tcg_gen_add_tl(daddr, daddr, four);
            }
        }
        break;

    default:
        {
            TCGv_i32 r_asi = tcg_constant_i32(da->asi);
            TCGv_i32 r_mop = tcg_constant_i32(da->memop | MO_ALIGN);

            save_state(dc);
#ifdef TARGET_SPARC64
            gen_helper_st_asi(tcg_env, addr, src, r_asi, r_mop);
#else
            {
                TCGv_i64 t64 = tcg_temp_new_i64();
                tcg_gen_extu_tl_i64(t64, src);
                gen_helper_st_asi(tcg_env, addr, t64, r_asi, r_mop);
            }
#endif

            /* A write to a TLB register may alter page maps.  End the TB. */
            dc->npc = DYNAMIC_PC;
        }
        break;
    }
}

static void __attribute__((unused))
gen_st_asi(DisasContext *dc, TCGv src, TCGv addr, int insn, MemOp memop)
{
    DisasASI da = get_asi(dc, insn, memop);

    gen_address_mask(dc, addr);
    gen_st_asi0(dc, &da, src, addr);
}

static void gen_swap_asi0(DisasContext *dc, DisasASI *da,
                          TCGv dst, TCGv src, TCGv addr)
{
    switch (da->type) {
    case GET_ASI_EXCP:
        break;
    case GET_ASI_DIRECT:
        gen_swap(dc, dst, src, addr, da->mem_idx, da->memop);
        break;
    default:
        /* ??? Should be DAE_invalid_asi.  */
        gen_exception(dc, TT_DATA_ACCESS);
        break;
    }
}

static void __attribute__((unused))
gen_swap_asi(DisasContext *dc, TCGv dst, TCGv src, TCGv addr, int insn)
{
    DisasASI da = get_asi(dc, insn, MO_TEUL);

    gen_address_mask(dc, addr);
    gen_swap_asi0(dc, &da, dst, src, addr);
}

static void gen_cas_asi0(DisasContext *dc, DisasASI *da,
                         TCGv oldv, TCGv newv, TCGv cmpv, TCGv addr)
{
    switch (da->type) {
    case GET_ASI_EXCP:
        return;
    case GET_ASI_DIRECT:
        tcg_gen_atomic_cmpxchg_tl(oldv, addr, cmpv, newv,
                                  da->mem_idx, da->memop | MO_ALIGN);
        break;
    default:
        /* ??? Should be DAE_invalid_asi.  */
        gen_exception(dc, TT_DATA_ACCESS);
        break;
    }
}

static void __attribute__((unused))
gen_cas_asi(DisasContext *dc, TCGv addr, TCGv cmpv, int insn, int rd)
{
    DisasASI da = get_asi(dc, insn, MO_TEUL);
    TCGv oldv = gen_dest_gpr(dc, rd);
    TCGv newv = gen_load_gpr(dc, rd);

    gen_address_mask(dc, addr);
    gen_cas_asi0(dc, &da, oldv, newv, cmpv, addr);
    gen_store_gpr(dc, rd, oldv);
}

static void __attribute__((unused))
gen_casx_asi(DisasContext *dc, TCGv addr, TCGv cmpv, int insn, int rd)
{
    DisasASI da = get_asi(dc, insn, MO_TEUQ);
    TCGv oldv = gen_dest_gpr(dc, rd);
    TCGv newv = gen_load_gpr(dc, rd);

    gen_address_mask(dc, addr);
    gen_cas_asi0(dc, &da, oldv, newv, cmpv, addr);
    gen_store_gpr(dc, rd, oldv);
}

static void gen_ldstub_asi0(DisasContext *dc, DisasASI *da, TCGv dst, TCGv addr)
{
    switch (da->type) {
    case GET_ASI_EXCP:
        break;
    case GET_ASI_DIRECT:
        gen_ldstub(dc, dst, addr, da->mem_idx);
        break;
    default:
        /* ??? In theory, this should be raise DAE_invalid_asi.
           But the SS-20 roms do ldstuba [%l0] #ASI_M_CTL, %o1.  */
        if (tb_cflags(dc->base.tb) & CF_PARALLEL) {
            gen_helper_exit_atomic(tcg_env);
        } else {
            TCGv_i32 r_asi = tcg_constant_i32(da->asi);
            TCGv_i32 r_mop = tcg_constant_i32(MO_UB);
            TCGv_i64 s64, t64;

            save_state(dc);
            t64 = tcg_temp_new_i64();
            gen_helper_ld_asi(t64, tcg_env, addr, r_asi, r_mop);

            s64 = tcg_constant_i64(0xff);
            gen_helper_st_asi(tcg_env, addr, s64, r_asi, r_mop);

            tcg_gen_trunc_i64_tl(dst, t64);

            /* End the TB.  */
            dc->npc = DYNAMIC_PC;
        }
        break;
    }
}

static void __attribute__((unused))
gen_ldstub_asi(DisasContext *dc, TCGv dst, TCGv addr, int insn)
{
    DisasASI da = get_asi(dc, insn, MO_UB);

    gen_address_mask(dc, addr);
    gen_ldstub_asi0(dc, &da, dst, addr);
}

static void __attribute__((unused))
gen_ldf_asi(DisasContext *dc, TCGv addr, int insn, int size, int rd)
{
    DisasASI da = get_asi(dc, insn, (size == 4 ? MO_TEUL : MO_TEUQ));
    TCGv_i32 d32;
    TCGv_i64 d64;

    switch (da.type) {
    case GET_ASI_EXCP:
        break;

    case GET_ASI_DIRECT:
        gen_address_mask(dc, addr);
        switch (size) {
        case 4:
            d32 = gen_dest_fpr_F(dc);
            tcg_gen_qemu_ld_i32(d32, addr, da.mem_idx, da.memop | MO_ALIGN);
            gen_store_fpr_F(dc, rd, d32);
            break;
        case 8:
            tcg_gen_qemu_ld_i64(cpu_fpr[rd / 2], addr, da.mem_idx,
                                da.memop | MO_ALIGN_4);
            break;
        case 16:
            d64 = tcg_temp_new_i64();
            tcg_gen_qemu_ld_i64(d64, addr, da.mem_idx, da.memop | MO_ALIGN_4);
            tcg_gen_addi_tl(addr, addr, 8);
            tcg_gen_qemu_ld_i64(cpu_fpr[rd/2+1], addr, da.mem_idx,
                                da.memop | MO_ALIGN_4);
            tcg_gen_mov_i64(cpu_fpr[rd / 2], d64);
            break;
        default:
            g_assert_not_reached();
        }
        break;

    case GET_ASI_BLOCK:
        /* Valid for lddfa on aligned registers only.  */
        if (size == 8 && (rd & 7) == 0) {
            MemOp memop;
            TCGv eight;
            int i;

            gen_address_mask(dc, addr);

            /* The first operation checks required alignment.  */
            memop = da.memop | MO_ALIGN_64;
            eight = tcg_constant_tl(8);
            for (i = 0; ; ++i) {
                tcg_gen_qemu_ld_i64(cpu_fpr[rd / 2 + i], addr,
                                    da.mem_idx, memop);
                if (i == 7) {
                    break;
                }
                tcg_gen_add_tl(addr, addr, eight);
                memop = da.memop;
            }
        } else {
            gen_exception(dc, TT_ILL_INSN);
        }
        break;

    case GET_ASI_SHORT:
        /* Valid for lddfa only.  */
        if (size == 8) {
            gen_address_mask(dc, addr);
            tcg_gen_qemu_ld_i64(cpu_fpr[rd / 2], addr, da.mem_idx,
                                da.memop | MO_ALIGN);
        } else {
            gen_exception(dc, TT_ILL_INSN);
        }
        break;

    default:
        {
            TCGv_i32 r_asi = tcg_constant_i32(da.asi);
            TCGv_i32 r_mop = tcg_constant_i32(da.memop | MO_ALIGN);

            save_state(dc);
            /* According to the table in the UA2011 manual, the only
               other asis that are valid for ldfa/lddfa/ldqfa are
               the NO_FAULT asis.  We still need a helper for these,
               but we can just use the integer asi helper for them.  */
            switch (size) {
            case 4:
                d64 = tcg_temp_new_i64();
                gen_helper_ld_asi(d64, tcg_env, addr, r_asi, r_mop);
                d32 = gen_dest_fpr_F(dc);
                tcg_gen_extrl_i64_i32(d32, d64);
                gen_store_fpr_F(dc, rd, d32);
                break;
            case 8:
                gen_helper_ld_asi(cpu_fpr[rd / 2], tcg_env, addr, r_asi, r_mop);
                break;
            case 16:
                d64 = tcg_temp_new_i64();
                gen_helper_ld_asi(d64, tcg_env, addr, r_asi, r_mop);
                tcg_gen_addi_tl(addr, addr, 8);
                gen_helper_ld_asi(cpu_fpr[rd/2+1], tcg_env, addr, r_asi, r_mop);
                tcg_gen_mov_i64(cpu_fpr[rd / 2], d64);
                break;
            default:
                g_assert_not_reached();
            }
        }
        break;
    }
}

static void __attribute__((unused))
gen_stf_asi(DisasContext *dc, TCGv addr, int insn, int size, int rd)
{
    DisasASI da = get_asi(dc, insn, (size == 4 ? MO_TEUL : MO_TEUQ));
    TCGv_i32 d32;

    switch (da.type) {
    case GET_ASI_EXCP:
        break;

    case GET_ASI_DIRECT:
        gen_address_mask(dc, addr);
        switch (size) {
        case 4:
            d32 = gen_load_fpr_F(dc, rd);
            tcg_gen_qemu_st_i32(d32, addr, da.mem_idx, da.memop | MO_ALIGN);
            break;
        case 8:
            tcg_gen_qemu_st_i64(cpu_fpr[rd / 2], addr, da.mem_idx,
                                da.memop | MO_ALIGN_4);
            break;
        case 16:
            /* Only 4-byte alignment required.  However, it is legal for the
               cpu to signal the alignment fault, and the OS trap handler is
               required to fix it up.  Requiring 16-byte alignment here avoids
               having to probe the second page before performing the first
               write.  */
            tcg_gen_qemu_st_i64(cpu_fpr[rd / 2], addr, da.mem_idx,
                                da.memop | MO_ALIGN_16);
            tcg_gen_addi_tl(addr, addr, 8);
            tcg_gen_qemu_st_i64(cpu_fpr[rd/2+1], addr, da.mem_idx, da.memop);
            break;
        default:
            g_assert_not_reached();
        }
        break;

    case GET_ASI_BLOCK:
        /* Valid for stdfa on aligned registers only.  */
        if (size == 8 && (rd & 7) == 0) {
            MemOp memop;
            TCGv eight;
            int i;

            gen_address_mask(dc, addr);

            /* The first operation checks required alignment.  */
            memop = da.memop | MO_ALIGN_64;
            eight = tcg_constant_tl(8);
            for (i = 0; ; ++i) {
                tcg_gen_qemu_st_i64(cpu_fpr[rd / 2 + i], addr,
                                    da.mem_idx, memop);
                if (i == 7) {
                    break;
                }
                tcg_gen_add_tl(addr, addr, eight);
                memop = da.memop;
            }
        } else {
            gen_exception(dc, TT_ILL_INSN);
        }
        break;

    case GET_ASI_SHORT:
        /* Valid for stdfa only.  */
        if (size == 8) {
            gen_address_mask(dc, addr);
            tcg_gen_qemu_st_i64(cpu_fpr[rd / 2], addr, da.mem_idx,
                                da.memop | MO_ALIGN);
        } else {
            gen_exception(dc, TT_ILL_INSN);
        }
        break;

    default:
        /* According to the table in the UA2011 manual, the only
           other asis that are valid for ldfa/lddfa/ldqfa are
           the PST* asis, which aren't currently handled.  */
        gen_exception(dc, TT_ILL_INSN);
        break;
    }
}

static void gen_ldda_asi0(DisasContext *dc, DisasASI *da, TCGv addr, int rd)
{
    TCGv hi = gen_dest_gpr(dc, rd);
    TCGv lo = gen_dest_gpr(dc, rd + 1);

    switch (da->type) {
    case GET_ASI_EXCP:
        return;

    case GET_ASI_DTWINX:
        assert(TARGET_LONG_BITS == 64);
        tcg_gen_qemu_ld_tl(hi, addr, da->mem_idx, da->memop | MO_ALIGN_16);
        tcg_gen_addi_tl(addr, addr, 8);
        tcg_gen_qemu_ld_tl(lo, addr, da->mem_idx, da->memop);
        break;

    case GET_ASI_DIRECT:
        {
            TCGv_i64 tmp = tcg_temp_new_i64();

            tcg_gen_qemu_ld_i64(tmp, addr, da->mem_idx, da->memop | MO_ALIGN);

            /* Note that LE ldda acts as if each 32-bit register
               result is byte swapped.  Having just performed one
               64-bit bswap, we need now to swap the writebacks.  */
            if ((da->memop & MO_BSWAP) == MO_TE) {
                tcg_gen_extr_i64_tl(lo, hi, tmp);
            } else {
                tcg_gen_extr_i64_tl(hi, lo, tmp);
            }
        }
        break;

    default:
        /* ??? In theory we've handled all of the ASIs that are valid
           for ldda, and this should raise DAE_invalid_asi.  However,
           real hardware allows others.  This can be seen with e.g.
           FreeBSD 10.3 wrt ASI_IC_TAG.  */
        {
            TCGv_i32 r_asi = tcg_constant_i32(da->asi);
            TCGv_i32 r_mop = tcg_constant_i32(da->memop);
            TCGv_i64 tmp = tcg_temp_new_i64();

            save_state(dc);
            gen_helper_ld_asi(tmp, tcg_env, addr, r_asi, r_mop);

            /* See above.  */
            if ((da->memop & MO_BSWAP) == MO_TE) {
                tcg_gen_extr_i64_tl(lo, hi, tmp);
            } else {
                tcg_gen_extr_i64_tl(hi, lo, tmp);
            }
        }
        break;
    }

    gen_store_gpr(dc, rd, hi);
    gen_store_gpr(dc, rd + 1, lo);
}

static void __attribute__((unused))
gen_ldda_asi(DisasContext *dc, TCGv addr, int insn, int rd)
{
    DisasASI da = get_asi(dc, insn, MO_TEUQ);

    gen_address_mask(dc, addr);
    gen_ldda_asi0(dc, &da, addr, rd);
}

static void gen_stda_asi0(DisasContext *dc, DisasASI *da, TCGv addr, int rd)
{
    TCGv hi = gen_load_gpr(dc, rd);
    TCGv lo = gen_load_gpr(dc, rd + 1);

    switch (da->type) {
    case GET_ASI_EXCP:
        break;

    case GET_ASI_DTWINX:
        assert(TARGET_LONG_BITS == 64);
        tcg_gen_qemu_st_tl(hi, addr, da->mem_idx, da->memop | MO_ALIGN_16);
        tcg_gen_addi_tl(addr, addr, 8);
        tcg_gen_qemu_st_tl(lo, addr, da->mem_idx, da->memop);
        break;

    case GET_ASI_DIRECT:
        {
            TCGv_i64 t64 = tcg_temp_new_i64();

            /* Note that LE stda acts as if each 32-bit register result is
               byte swapped.  We will perform one 64-bit LE store, so now
               we must swap the order of the construction.  */
            if ((da->memop & MO_BSWAP) == MO_TE) {
                tcg_gen_concat_tl_i64(t64, lo, hi);
            } else {
                tcg_gen_concat_tl_i64(t64, hi, lo);
            }
            tcg_gen_qemu_st_i64(t64, addr, da->mem_idx, da->memop | MO_ALIGN);
        }
        break;

    case GET_ASI_BFILL:
        assert(TARGET_LONG_BITS == 32);
        /* Store 32 bytes of T64 to ADDR.  */
        /* ??? The original qemu code suggests 8-byte alignment, dropping
           the low bits, but the only place I can see this used is in the
           Linux kernel with 32 byte alignment, which would make more sense
           as a cacheline-style operation.  */
        {
            TCGv_i64 t64 = tcg_temp_new_i64();
            TCGv d_addr = tcg_temp_new();
            TCGv eight = tcg_constant_tl(8);
            int i;

            tcg_gen_concat_tl_i64(t64, lo, hi);
            tcg_gen_andi_tl(d_addr, addr, -8);
            for (i = 0; i < 32; i += 8) {
                tcg_gen_qemu_st_i64(t64, d_addr, da->mem_idx, da->memop);
                tcg_gen_add_tl(d_addr, d_addr, eight);
            }
        }
        break;

    default:
        /* ??? In theory we've handled all of the ASIs that are valid
           for stda, and this should raise DAE_invalid_asi.  */
        {
            TCGv_i32 r_asi = tcg_constant_i32(da->asi);
            TCGv_i32 r_mop = tcg_constant_i32(da->memop);
            TCGv_i64 t64 = tcg_temp_new_i64();

            /* See above.  */
            if ((da->memop & MO_BSWAP) == MO_TE) {
                tcg_gen_concat_tl_i64(t64, lo, hi);
            } else {
                tcg_gen_concat_tl_i64(t64, hi, lo);
            }

            save_state(dc);
            gen_helper_st_asi(tcg_env, addr, t64, r_asi, r_mop);
        }
        break;
    }
}

static void __attribute__((unused))
gen_stda_asi(DisasContext *dc, TCGv hi, TCGv addr, int insn, int rd)
{
    DisasASI da = get_asi(dc, insn, MO_TEUQ);

    gen_address_mask(dc, addr);
    gen_stda_asi0(dc, &da, addr, rd);
}

static TCGv get_src1(DisasContext *dc, unsigned int insn)
{
    unsigned int rs1 = GET_FIELD(insn, 13, 17);
    return gen_load_gpr(dc, rs1);
}

#ifdef TARGET_SPARC64
static void gen_fmovs(DisasContext *dc, DisasCompare *cmp, int rd, int rs)
{
    TCGv_i32 c32, zero, dst, s1, s2;

    /* We have two choices here: extend the 32 bit data and use movcond_i64,
       or fold the comparison down to 32 bits and use movcond_i32.  Choose
       the later.  */
    c32 = tcg_temp_new_i32();
    if (cmp->is_bool) {
        tcg_gen_extrl_i64_i32(c32, cmp->c1);
    } else {
        TCGv_i64 c64 = tcg_temp_new_i64();
        tcg_gen_setcond_i64(cmp->cond, c64, cmp->c1, cmp->c2);
        tcg_gen_extrl_i64_i32(c32, c64);
    }

    s1 = gen_load_fpr_F(dc, rs);
    s2 = gen_load_fpr_F(dc, rd);
    dst = gen_dest_fpr_F(dc);
    zero = tcg_constant_i32(0);

    tcg_gen_movcond_i32(TCG_COND_NE, dst, c32, zero, s1, s2);

    gen_store_fpr_F(dc, rd, dst);
}

static void gen_fmovd(DisasContext *dc, DisasCompare *cmp, int rd, int rs)
{
    TCGv_i64 dst = gen_dest_fpr_D(dc, rd);
    tcg_gen_movcond_i64(cmp->cond, dst, cmp->c1, cmp->c2,
                        gen_load_fpr_D(dc, rs),
                        gen_load_fpr_D(dc, rd));
    gen_store_fpr_D(dc, rd, dst);
}

static void gen_fmovq(DisasContext *dc, DisasCompare *cmp, int rd, int rs)
{
    int qd = QFPREG(rd);
    int qs = QFPREG(rs);

    tcg_gen_movcond_i64(cmp->cond, cpu_fpr[qd / 2], cmp->c1, cmp->c2,
                        cpu_fpr[qs / 2], cpu_fpr[qd / 2]);
    tcg_gen_movcond_i64(cmp->cond, cpu_fpr[qd / 2 + 1], cmp->c1, cmp->c2,
                        cpu_fpr[qs / 2 + 1], cpu_fpr[qd / 2 + 1]);

    gen_update_fprs_dirty(dc, qd);
}

static void gen_load_trap_state_at_tl(TCGv_ptr r_tsptr)
{
    TCGv_i32 r_tl = tcg_temp_new_i32();

    /* load env->tl into r_tl */
    tcg_gen_ld_i32(r_tl, tcg_env, offsetof(CPUSPARCState, tl));

    /* tl = [0 ... MAXTL_MASK] where MAXTL_MASK must be power of 2 */
    tcg_gen_andi_i32(r_tl, r_tl, MAXTL_MASK);

    /* calculate offset to current trap state from env->ts, reuse r_tl */
    tcg_gen_muli_i32(r_tl, r_tl, sizeof (trap_state));
    tcg_gen_addi_ptr(r_tsptr, tcg_env, offsetof(CPUSPARCState, ts));

    /* tsptr = env->ts[env->tl & MAXTL_MASK] */
    {
        TCGv_ptr r_tl_tmp = tcg_temp_new_ptr();
        tcg_gen_ext_i32_ptr(r_tl_tmp, r_tl);
        tcg_gen_add_ptr(r_tsptr, r_tsptr, r_tl_tmp);
    }
}

static void gen_edge(DisasContext *dc, TCGv dst, TCGv s1, TCGv s2,
                     int width, bool cc, bool left)
{
    TCGv lo1, lo2;
    uint64_t amask, tabl, tabr;
    int shift, imask, omask;

    if (cc) {
        tcg_gen_mov_tl(cpu_cc_src, s1);
        tcg_gen_mov_tl(cpu_cc_src2, s2);
        tcg_gen_sub_tl(cpu_cc_dst, s1, s2);
        tcg_gen_movi_i32(cpu_cc_op, CC_OP_SUB);
        dc->cc_op = CC_OP_SUB;
    }

    /* Theory of operation: there are two tables, left and right (not to
       be confused with the left and right versions of the opcode).  These
       are indexed by the low 3 bits of the inputs.  To make things "easy",
       these tables are loaded into two constants, TABL and TABR below.
       The operation index = (input & imask) << shift calculates the index
       into the constant, while val = (table >> index) & omask calculates
       the value we're looking for.  */
    switch (width) {
    case 8:
        imask = 0x7;
        shift = 3;
        omask = 0xff;
        if (left) {
            tabl = 0x80c0e0f0f8fcfeffULL;
            tabr = 0xff7f3f1f0f070301ULL;
        } else {
            tabl = 0x0103070f1f3f7fffULL;
            tabr = 0xfffefcf8f0e0c080ULL;
        }
        break;
    case 16:
        imask = 0x6;
        shift = 1;
        omask = 0xf;
        if (left) {
            tabl = 0x8cef;
            tabr = 0xf731;
        } else {
            tabl = 0x137f;
            tabr = 0xfec8;
        }
        break;
    case 32:
        imask = 0x4;
        shift = 0;
        omask = 0x3;
        if (left) {
            tabl = (2 << 2) | 3;
            tabr = (3 << 2) | 1;
        } else {
            tabl = (1 << 2) | 3;
            tabr = (3 << 2) | 2;
        }
        break;
    default:
        abort();
    }

    lo1 = tcg_temp_new();
    lo2 = tcg_temp_new();
    tcg_gen_andi_tl(lo1, s1, imask);
    tcg_gen_andi_tl(lo2, s2, imask);
    tcg_gen_shli_tl(lo1, lo1, shift);
    tcg_gen_shli_tl(lo2, lo2, shift);

    tcg_gen_shr_tl(lo1, tcg_constant_tl(tabl), lo1);
    tcg_gen_shr_tl(lo2, tcg_constant_tl(tabr), lo2);
    tcg_gen_andi_tl(lo1, lo1, omask);
    tcg_gen_andi_tl(lo2, lo2, omask);

    amask = -8;
    if (AM_CHECK(dc)) {
        amask &= 0xffffffffULL;
    }
    tcg_gen_andi_tl(s1, s1, amask);
    tcg_gen_andi_tl(s2, s2, amask);

    /* Compute dst = (s1 == s2 ? lo1 : lo1 & lo2). */
    tcg_gen_and_tl(lo2, lo2, lo1);
    tcg_gen_movcond_tl(TCG_COND_EQ, dst, s1, s2, lo1, lo2);
}

static void gen_alignaddr(TCGv dst, TCGv s1, TCGv s2, bool left)
{
    TCGv tmp = tcg_temp_new();

    tcg_gen_add_tl(tmp, s1, s2);
    tcg_gen_andi_tl(dst, tmp, -8);
    if (left) {
        tcg_gen_neg_tl(tmp, tmp);
    }
    tcg_gen_deposit_tl(cpu_gsr, cpu_gsr, tmp, 0, 3);
}

static void gen_faligndata(TCGv dst, TCGv gsr, TCGv s1, TCGv s2)
{
    TCGv t1, t2, shift;

    t1 = tcg_temp_new();
    t2 = tcg_temp_new();
    shift = tcg_temp_new();

    tcg_gen_andi_tl(shift, gsr, 7);
    tcg_gen_shli_tl(shift, shift, 3);
    tcg_gen_shl_tl(t1, s1, shift);

    /* A shift of 64 does not produce 0 in TCG.  Divide this into a
       shift of (up to 63) followed by a constant shift of 1.  */
    tcg_gen_xori_tl(shift, shift, 63);
    tcg_gen_shr_tl(t2, s2, shift);
    tcg_gen_shri_tl(t2, t2, 1);

    tcg_gen_or_tl(dst, t1, t2);
}
#endif

/* Include the auto-generated decoder.  */
#include "decode-insns.c.inc"

#define TRANS(NAME, AVAIL, FUNC, ...) \
    static bool trans_##NAME(DisasContext *dc, arg_##NAME *a) \
    { return avail_##AVAIL(dc) && FUNC(dc, __VA_ARGS__); }

#define avail_ALL(C)      true
#ifdef TARGET_SPARC64
# define avail_32(C)      false
# define avail_ASR17(C)   false
# define avail_DIV(C)     true
# define avail_MUL(C)     true
# define avail_POWERDOWN(C) false
# define avail_64(C)      true
# define avail_GL(C)      ((C)->def->features & CPU_FEATURE_GL)
# define avail_HYPV(C)    ((C)->def->features & CPU_FEATURE_HYPV)
#else
# define avail_32(C)      true
# define avail_ASR17(C)   ((C)->def->features & CPU_FEATURE_ASR17)
# define avail_DIV(C)     ((C)->def->features & CPU_FEATURE_DIV)
# define avail_MUL(C)     ((C)->def->features & CPU_FEATURE_MUL)
# define avail_POWERDOWN(C) ((C)->def->features & CPU_FEATURE_POWERDOWN)
# define avail_64(C)      false
# define avail_GL(C)      false
# define avail_HYPV(C)    false
#endif

/* Default case for non jump instructions. */
static bool advance_pc(DisasContext *dc)
{
    if (dc->npc & 3) {
        switch (dc->npc) {
        case DYNAMIC_PC:
        case DYNAMIC_PC_LOOKUP:
            dc->pc = dc->npc;
            gen_op_next_insn();
            break;
        case JUMP_PC:
            /* we can do a static jump */
            gen_branch2(dc, dc->jump_pc[0], dc->jump_pc[1], cpu_cond);
            dc->base.is_jmp = DISAS_NORETURN;
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        dc->pc = dc->npc;
        dc->npc = dc->npc + 4;
    }
    return true;
}

/*
 * Major opcodes 00 and 01 -- branches, call, and sethi
 */

static bool advance_jump_uncond_never(DisasContext *dc, bool annul)
{
    if (annul) {
        dc->pc = dc->npc + 4;
        dc->npc = dc->pc + 4;
    } else {
        dc->pc = dc->npc;
        dc->npc = dc->pc + 4;
    }
    return true;
}

static bool advance_jump_uncond_always(DisasContext *dc, bool annul,
                                       target_ulong dest)
{
    if (annul) {
        dc->pc = dest;
        dc->npc = dest + 4;
    } else {
        dc->pc = dc->npc;
        dc->npc = dest;
        tcg_gen_mov_tl(cpu_pc, cpu_npc);
    }
    return true;
}

static bool advance_jump_cond(DisasContext *dc, DisasCompare *cmp,
                              bool annul, target_ulong dest)
{
    target_ulong npc = dc->npc;

    if (annul) {
        TCGLabel *l1 = gen_new_label();

        tcg_gen_brcond_tl(tcg_invert_cond(cmp->cond), cmp->c1, cmp->c2, l1);
        gen_goto_tb(dc, 0, npc, dest);
        gen_set_label(l1);
        gen_goto_tb(dc, 1, npc + 4, npc + 8);

        dc->base.is_jmp = DISAS_NORETURN;
    } else {
        if (npc & 3) {
            switch (npc) {
            case DYNAMIC_PC:
            case DYNAMIC_PC_LOOKUP:
                tcg_gen_mov_tl(cpu_pc, cpu_npc);
                tcg_gen_addi_tl(cpu_npc, cpu_npc, 4);
                tcg_gen_movcond_tl(cmp->cond, cpu_npc,
                                   cmp->c1, cmp->c2,
                                   tcg_constant_tl(dest), cpu_npc);
                dc->pc = npc;
                break;
            default:
                g_assert_not_reached();
            }
        } else {
            dc->pc = npc;
            dc->jump_pc[0] = dest;
            dc->jump_pc[1] = npc + 4;
            dc->npc = JUMP_PC;
            if (cmp->is_bool) {
                tcg_gen_mov_tl(cpu_cond, cmp->c1);
            } else {
                tcg_gen_setcond_tl(cmp->cond, cpu_cond, cmp->c1, cmp->c2);
            }
        }
    }
    return true;
}

static bool raise_priv(DisasContext *dc)
{
    gen_exception(dc, TT_PRIV_INSN);
    return true;
}

static bool do_bpcc(DisasContext *dc, arg_bcc *a)
{
    target_long target = address_mask_i(dc, dc->pc + a->i * 4);
    DisasCompare cmp;

    switch (a->cond) {
    case 0x0:
        return advance_jump_uncond_never(dc, a->a);
    case 0x8:
        return advance_jump_uncond_always(dc, a->a, target);
    default:
        flush_cond(dc);

        gen_compare(&cmp, a->cc, a->cond, dc);
        return advance_jump_cond(dc, &cmp, a->a, target);
    }
}

TRANS(Bicc, ALL, do_bpcc, a)
TRANS(BPcc,  64, do_bpcc, a)

static bool do_fbpfcc(DisasContext *dc, arg_bcc *a)
{
    target_long target = address_mask_i(dc, dc->pc + a->i * 4);
    DisasCompare cmp;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }
    switch (a->cond) {
    case 0x0:
        return advance_jump_uncond_never(dc, a->a);
    case 0x8:
        return advance_jump_uncond_always(dc, a->a, target);
    default:
        flush_cond(dc);

        gen_fcompare(&cmp, a->cc, a->cond);
        return advance_jump_cond(dc, &cmp, a->a, target);
    }
}

TRANS(FBPfcc,  64, do_fbpfcc, a)
TRANS(FBfcc,  ALL, do_fbpfcc, a)

static bool trans_BPr(DisasContext *dc, arg_BPr *a)
{
    target_long target = address_mask_i(dc, dc->pc + a->i * 4);
    DisasCompare cmp;

    if (!avail_64(dc)) {
        return false;
    }
    if (gen_tcg_cond_reg[a->cond] == TCG_COND_NEVER) {
        return false;
    }

    flush_cond(dc);
    gen_compare_reg(&cmp, a->cond, gen_load_gpr(dc, a->rs1));
    return advance_jump_cond(dc, &cmp, a->a, target);
}

static bool trans_CALL(DisasContext *dc, arg_CALL *a)
{
    target_long target = address_mask_i(dc, dc->pc + a->i * 4);

    gen_store_gpr(dc, 15, tcg_constant_tl(dc->pc));
    gen_mov_pc_npc(dc);
    dc->npc = target;
    return true;
}

static bool trans_NCP(DisasContext *dc, arg_NCP *a)
{
    /*
     * For sparc32, always generate the no-coprocessor exception.
     * For sparc64, always generate illegal instruction.
     */
#ifdef TARGET_SPARC64
    return false;
#else
    gen_exception(dc, TT_NCP_INSN);
    return true;
#endif
}

static bool trans_SETHI(DisasContext *dc, arg_SETHI *a)
{
    /* Special-case %g0 because that's the canonical nop.  */
    if (a->rd) {
        gen_store_gpr(dc, a->rd, tcg_constant_tl((uint32_t)a->i << 10));
    }
    return advance_pc(dc);
}

/*
 * Major Opcode 10 -- integer, floating-point, vis, and system insns.
 */

static bool do_tcc(DisasContext *dc, int cond, int cc,
                   int rs1, bool imm, int rs2_or_imm)
{
    int mask = ((dc->def->features & CPU_FEATURE_HYPV) && supervisor(dc)
                ? UA2005_HTRAP_MASK : V8_TRAP_MASK);
    DisasCompare cmp;
    TCGLabel *lab;
    TCGv_i32 trap;

    /* Trap never.  */
    if (cond == 0) {
        return advance_pc(dc);
    }

    /*
     * Immediate traps are the most common case.  Since this value is
     * live across the branch, it really pays to evaluate the constant.
     */
    if (rs1 == 0 && (imm || rs2_or_imm == 0)) {
        trap = tcg_constant_i32((rs2_or_imm & mask) + TT_TRAP);
    } else {
        trap = tcg_temp_new_i32();
        tcg_gen_trunc_tl_i32(trap, gen_load_gpr(dc, rs1));
        if (imm) {
            tcg_gen_addi_i32(trap, trap, rs2_or_imm);
        } else {
            TCGv_i32 t2 = tcg_temp_new_i32();
            tcg_gen_trunc_tl_i32(t2, gen_load_gpr(dc, rs2_or_imm));
            tcg_gen_add_i32(trap, trap, t2);
        }
        tcg_gen_andi_i32(trap, trap, mask);
        tcg_gen_addi_i32(trap, trap, TT_TRAP);
    }

    /* Trap always.  */
    if (cond == 8) {
        save_state(dc);
        gen_helper_raise_exception(tcg_env, trap);
        dc->base.is_jmp = DISAS_NORETURN;
        return true;
    }

    /* Conditional trap.  */
    flush_cond(dc);
    lab = delay_exceptionv(dc, trap);
    gen_compare(&cmp, cc, cond, dc);
    tcg_gen_brcond_tl(cmp.cond, cmp.c1, cmp.c2, lab);

    return advance_pc(dc);
}

static bool trans_Tcc_r(DisasContext *dc, arg_Tcc_r *a)
{
    if (avail_32(dc) && a->cc) {
        return false;
    }
    return do_tcc(dc, a->cond, a->cc, a->rs1, false, a->rs2);
}

static bool trans_Tcc_i_v7(DisasContext *dc, arg_Tcc_i_v7 *a)
{
    if (avail_64(dc)) {
        return false;
    }
    return do_tcc(dc, a->cond, 0, a->rs1, true, a->i);
}

static bool trans_Tcc_i_v9(DisasContext *dc, arg_Tcc_i_v9 *a)
{
    if (avail_32(dc)) {
        return false;
    }
    return do_tcc(dc, a->cond, a->cc, a->rs1, true, a->i);
}

static bool trans_STBAR(DisasContext *dc, arg_STBAR *a)
{
    tcg_gen_mb(TCG_MO_ST_ST | TCG_BAR_SC);
    return advance_pc(dc);
}

static bool trans_MEMBAR(DisasContext *dc, arg_MEMBAR *a)
{
    if (avail_32(dc)) {
        return false;
    }
    if (a->mmask) {
        /* Note TCG_MO_* was modeled on sparc64, so mmask matches. */
        tcg_gen_mb(a->mmask | TCG_BAR_SC);
    }
    if (a->cmask) {
        /* For #Sync, etc, end the TB to recognize interrupts. */
        dc->base.is_jmp = DISAS_EXIT;
    }
    return advance_pc(dc);
}

static bool do_rd_special(DisasContext *dc, bool priv, int rd,
                          TCGv (*func)(DisasContext *, TCGv))
{
    if (!priv) {
        return raise_priv(dc);
    }
    gen_store_gpr(dc, rd, func(dc, gen_dest_gpr(dc, rd)));
    return advance_pc(dc);
}

static TCGv do_rdy(DisasContext *dc, TCGv dst)
{
    return cpu_y;
}

static bool trans_RDY(DisasContext *dc, arg_RDY *a)
{
    /*
     * TODO: Need a feature bit for sparcv8.  In the meantime, treat all
     * 32-bit cpus like sparcv7, which ignores the rs1 field.
     * This matches after all other ASR, so Leon3 Asr17 is handled first.
     */
    if (avail_64(dc) && a->rs1 != 0) {
        return false;
    }
    return do_rd_special(dc, true, a->rd, do_rdy);
}

static TCGv do_rd_leon3_config(DisasContext *dc, TCGv dst)
{
    uint32_t val;

    /*
     * TODO: There are many more fields to be filled,
     * some of which are writable.
     */
    val = dc->def->nwindows - 1;   /* [4:0] NWIN */
    val |= 1 << 8;                 /* [8]   V8   */

    return tcg_constant_tl(val);
}

TRANS(RDASR17, ASR17, do_rd_special, true, a->rd, do_rd_leon3_config)

static TCGv do_rdccr(DisasContext *dc, TCGv dst)
{
    update_psr(dc);
    gen_helper_rdccr(dst, tcg_env);
    return dst;
}

TRANS(RDCCR, 64, do_rd_special, true, a->rd, do_rdccr)

static TCGv do_rdasi(DisasContext *dc, TCGv dst)
{
#ifdef TARGET_SPARC64
    return tcg_constant_tl(dc->asi);
#else
    qemu_build_not_reached();
#endif
}

TRANS(RDASI, 64, do_rd_special, true, a->rd, do_rdasi)

static TCGv do_rdtick(DisasContext *dc, TCGv dst)
{
    TCGv_ptr r_tickptr = tcg_temp_new_ptr();

    tcg_gen_ld_ptr(r_tickptr, tcg_env, env64_field_offsetof(tick));
    if (translator_io_start(&dc->base)) {
        dc->base.is_jmp = DISAS_EXIT;
    }
    gen_helper_tick_get_count(dst, tcg_env, r_tickptr,
                              tcg_constant_i32(dc->mem_idx));
    return dst;
}

/* TODO: non-priv access only allowed when enabled. */
TRANS(RDTICK, 64, do_rd_special, true, a->rd, do_rdtick)

static TCGv do_rdpc(DisasContext *dc, TCGv dst)
{
    return tcg_constant_tl(address_mask_i(dc, dc->pc));
}

TRANS(RDPC, 64, do_rd_special, true, a->rd, do_rdpc)

static TCGv do_rdfprs(DisasContext *dc, TCGv dst)
{
    tcg_gen_ext_i32_tl(dst, cpu_fprs);
    return dst;
}

TRANS(RDFPRS, 64, do_rd_special, true, a->rd, do_rdfprs)

static TCGv do_rdgsr(DisasContext *dc, TCGv dst)
{
    gen_trap_ifnofpu(dc);
    return cpu_gsr;
}

TRANS(RDGSR, 64, do_rd_special, true, a->rd, do_rdgsr)

static TCGv do_rdsoftint(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld32s_tl(dst, tcg_env, env64_field_offsetof(softint));
    return dst;
}

TRANS(RDSOFTINT, 64, do_rd_special, supervisor(dc), a->rd, do_rdsoftint)

static TCGv do_rdtick_cmpr(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld_tl(dst, tcg_env, env64_field_offsetof(tick_cmpr));
    return dst;
}

/* TODO: non-priv access only allowed when enabled. */
TRANS(RDTICK_CMPR, 64, do_rd_special, true, a->rd, do_rdtick_cmpr)

static TCGv do_rdstick(DisasContext *dc, TCGv dst)
{
    TCGv_ptr r_tickptr = tcg_temp_new_ptr();

    tcg_gen_ld_ptr(r_tickptr, tcg_env, env64_field_offsetof(stick));
    if (translator_io_start(&dc->base)) {
        dc->base.is_jmp = DISAS_EXIT;
    }
    gen_helper_tick_get_count(dst, tcg_env, r_tickptr,
                              tcg_constant_i32(dc->mem_idx));
    return dst;
}

/* TODO: non-priv access only allowed when enabled. */
TRANS(RDSTICK, 64, do_rd_special, true, a->rd, do_rdstick)

static TCGv do_rdstick_cmpr(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld_tl(dst, tcg_env, env64_field_offsetof(stick_cmpr));
    return dst;
}

/* TODO: supervisor access only allowed when enabled by hypervisor. */
TRANS(RDSTICK_CMPR, 64, do_rd_special, supervisor(dc), a->rd, do_rdstick_cmpr)

/*
 * UltraSPARC-T1 Strand status.
 * HYPV check maybe not enough, UA2005 & UA2007 describe
 * this ASR as impl. dep
 */
static TCGv do_rdstrand_status(DisasContext *dc, TCGv dst)
{
    return tcg_constant_tl(1);
}

TRANS(RDSTRAND_STATUS, HYPV, do_rd_special, true, a->rd, do_rdstrand_status)

static TCGv do_rdpsr(DisasContext *dc, TCGv dst)
{
    update_psr(dc);
    gen_helper_rdpsr(dst, tcg_env);
    return dst;
}

TRANS(RDPSR, 32, do_rd_special, supervisor(dc), a->rd, do_rdpsr)

static TCGv do_rdhpstate(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld_tl(dst, tcg_env, env64_field_offsetof(hpstate));
    return dst;
}

TRANS(RDHPR_hpstate, HYPV, do_rd_special, hypervisor(dc), a->rd, do_rdhpstate)

static TCGv do_rdhtstate(DisasContext *dc, TCGv dst)
{
    TCGv_i32 tl = tcg_temp_new_i32();
    TCGv_ptr tp = tcg_temp_new_ptr();

    tcg_gen_ld_i32(tl, tcg_env, env64_field_offsetof(tl));
    tcg_gen_andi_i32(tl, tl, MAXTL_MASK);
    tcg_gen_shli_i32(tl, tl, 3);
    tcg_gen_ext_i32_ptr(tp, tl);
    tcg_gen_add_ptr(tp, tp, tcg_env);

    tcg_gen_ld_tl(dst, tp, env64_field_offsetof(htstate));
    return dst;
}

TRANS(RDHPR_htstate, HYPV, do_rd_special, hypervisor(dc), a->rd, do_rdhtstate)

static TCGv do_rdhintp(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld_tl(dst, tcg_env, env64_field_offsetof(hintp));
    return dst;
}

TRANS(RDHPR_hintp, HYPV, do_rd_special, hypervisor(dc), a->rd, do_rdhintp)

static TCGv do_rdhtba(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld_tl(dst, tcg_env, env64_field_offsetof(htba));
    return dst;
}

TRANS(RDHPR_htba, HYPV, do_rd_special, hypervisor(dc), a->rd, do_rdhtba)

static TCGv do_rdhver(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld_tl(dst, tcg_env, env64_field_offsetof(hver));
    return dst;
}

TRANS(RDHPR_hver, HYPV, do_rd_special, hypervisor(dc), a->rd, do_rdhver)

static TCGv do_rdhstick_cmpr(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld_tl(dst, tcg_env, env64_field_offsetof(hstick_cmpr));
    return dst;
}

TRANS(RDHPR_hstick_cmpr, HYPV, do_rd_special, hypervisor(dc), a->rd,
      do_rdhstick_cmpr)

static TCGv do_rdwim(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld_tl(dst, tcg_env, env32_field_offsetof(wim));
    return dst;
}

TRANS(RDWIM, 32, do_rd_special, supervisor(dc), a->rd, do_rdwim)

static TCGv do_rdtpc(DisasContext *dc, TCGv dst)
{
#ifdef TARGET_SPARC64
    TCGv_ptr r_tsptr = tcg_temp_new_ptr();

    gen_load_trap_state_at_tl(r_tsptr);
    tcg_gen_ld_tl(dst, r_tsptr, offsetof(trap_state, tpc));
    return dst;
#else
    qemu_build_not_reached();
#endif
}

TRANS(RDPR_tpc, 64, do_rd_special, supervisor(dc), a->rd, do_rdtpc)

static TCGv do_rdtnpc(DisasContext *dc, TCGv dst)
{
#ifdef TARGET_SPARC64
    TCGv_ptr r_tsptr = tcg_temp_new_ptr();

    gen_load_trap_state_at_tl(r_tsptr);
    tcg_gen_ld_tl(dst, r_tsptr, offsetof(trap_state, tnpc));
    return dst;
#else
    qemu_build_not_reached();
#endif
}

TRANS(RDPR_tnpc, 64, do_rd_special, supervisor(dc), a->rd, do_rdtnpc)

static TCGv do_rdtstate(DisasContext *dc, TCGv dst)
{
#ifdef TARGET_SPARC64
    TCGv_ptr r_tsptr = tcg_temp_new_ptr();

    gen_load_trap_state_at_tl(r_tsptr);
    tcg_gen_ld_tl(dst, r_tsptr, offsetof(trap_state, tstate));
    return dst;
#else
    qemu_build_not_reached();
#endif
}

TRANS(RDPR_tstate, 64, do_rd_special, supervisor(dc), a->rd, do_rdtstate)

static TCGv do_rdtt(DisasContext *dc, TCGv dst)
{
#ifdef TARGET_SPARC64
    TCGv_ptr r_tsptr = tcg_temp_new_ptr();

    gen_load_trap_state_at_tl(r_tsptr);
    tcg_gen_ld32s_tl(dst, r_tsptr, offsetof(trap_state, tt));
    return dst;
#else
    qemu_build_not_reached();
#endif
}

TRANS(RDPR_tt, 64, do_rd_special, supervisor(dc), a->rd, do_rdtt)
TRANS(RDPR_tick, 64, do_rd_special, supervisor(dc), a->rd, do_rdtick)

static TCGv do_rdtba(DisasContext *dc, TCGv dst)
{
    return cpu_tbr;
}

TRANS(RDTBR, 32, do_rd_special, supervisor(dc), a->rd, do_rdtba)
TRANS(RDPR_tba, 64, do_rd_special, supervisor(dc), a->rd, do_rdtba)

static TCGv do_rdpstate(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld32s_tl(dst, tcg_env, env64_field_offsetof(pstate));
    return dst;
}

TRANS(RDPR_pstate, 64, do_rd_special, supervisor(dc), a->rd, do_rdpstate)

static TCGv do_rdtl(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld32s_tl(dst, tcg_env, env64_field_offsetof(tl));
    return dst;
}

TRANS(RDPR_tl, 64, do_rd_special, supervisor(dc), a->rd, do_rdtl)

static TCGv do_rdpil(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld32s_tl(dst, tcg_env, env_field_offsetof(psrpil));
    return dst;
}

TRANS(RDPR_pil, 64, do_rd_special, supervisor(dc), a->rd, do_rdpil)

static TCGv do_rdcwp(DisasContext *dc, TCGv dst)
{
    gen_helper_rdcwp(dst, tcg_env);
    return dst;
}

TRANS(RDPR_cwp, 64, do_rd_special, supervisor(dc), a->rd, do_rdcwp)

static TCGv do_rdcansave(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld32s_tl(dst, tcg_env, env64_field_offsetof(cansave));
    return dst;
}

TRANS(RDPR_cansave, 64, do_rd_special, supervisor(dc), a->rd, do_rdcansave)

static TCGv do_rdcanrestore(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld32s_tl(dst, tcg_env, env64_field_offsetof(canrestore));
    return dst;
}

TRANS(RDPR_canrestore, 64, do_rd_special, supervisor(dc), a->rd,
      do_rdcanrestore)

static TCGv do_rdcleanwin(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld32s_tl(dst, tcg_env, env64_field_offsetof(cleanwin));
    return dst;
}

TRANS(RDPR_cleanwin, 64, do_rd_special, supervisor(dc), a->rd, do_rdcleanwin)

static TCGv do_rdotherwin(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld32s_tl(dst, tcg_env, env64_field_offsetof(otherwin));
    return dst;
}

TRANS(RDPR_otherwin, 64, do_rd_special, supervisor(dc), a->rd, do_rdotherwin)

static TCGv do_rdwstate(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld32s_tl(dst, tcg_env, env64_field_offsetof(wstate));
    return dst;
}

TRANS(RDPR_wstate, 64, do_rd_special, supervisor(dc), a->rd, do_rdwstate)

static TCGv do_rdgl(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld32s_tl(dst, tcg_env, env64_field_offsetof(gl));
    return dst;
}

TRANS(RDPR_gl, GL, do_rd_special, supervisor(dc), a->rd, do_rdgl)

/* UA2005 strand status */
static TCGv do_rdssr(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld_tl(dst, tcg_env, env64_field_offsetof(ssr));
    return dst;
}

TRANS(RDPR_strand_status, HYPV, do_rd_special, hypervisor(dc), a->rd, do_rdssr)

static TCGv do_rdver(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld_tl(dst, tcg_env, env64_field_offsetof(version));
    return dst;
}

TRANS(RDPR_ver, 64, do_rd_special, supervisor(dc), a->rd, do_rdver)

static bool trans_FLUSHW(DisasContext *dc, arg_FLUSHW *a)
{
    if (avail_64(dc)) {
        gen_helper_flushw(tcg_env);
        return advance_pc(dc);
    }
    return false;
}

static bool do_wr_special(DisasContext *dc, arg_r_r_ri *a, bool priv,
                          void (*func)(DisasContext *, TCGv))
{
    TCGv src;

    /* For simplicity, we under-decoded the rs2 form. */
    if (!a->imm && (a->rs2_or_imm & ~0x1f)) {
        return false;
    }
    if (!priv) {
        return raise_priv(dc);
    }

    if (a->rs1 == 0 && (a->imm || a->rs2_or_imm == 0)) {
        src = tcg_constant_tl(a->rs2_or_imm);
    } else {
        TCGv src1 = gen_load_gpr(dc, a->rs1);
        if (a->rs2_or_imm == 0) {
            src = src1;
        } else {
            src = tcg_temp_new();
            if (a->imm) {
                tcg_gen_xori_tl(src, src1, a->rs2_or_imm);
            } else {
                tcg_gen_xor_tl(src, src1, gen_load_gpr(dc, a->rs2_or_imm));
            }
        }
    }
    func(dc, src);
    return advance_pc(dc);
}

static void do_wry(DisasContext *dc, TCGv src)
{
    tcg_gen_ext32u_tl(cpu_y, src);
}

TRANS(WRY, ALL, do_wr_special, a, true, do_wry)

static void do_wrccr(DisasContext *dc, TCGv src)
{
    gen_helper_wrccr(tcg_env, src);
}

TRANS(WRCCR, 64, do_wr_special, a, true, do_wrccr)

static void do_wrasi(DisasContext *dc, TCGv src)
{
    TCGv tmp = tcg_temp_new();

    tcg_gen_ext8u_tl(tmp, src);
    tcg_gen_st32_tl(tmp, tcg_env, env64_field_offsetof(asi));
    /* End TB to notice changed ASI. */
    dc->base.is_jmp = DISAS_EXIT;
}

TRANS(WRASI, 64, do_wr_special, a, true, do_wrasi)

static void do_wrfprs(DisasContext *dc, TCGv src)
{
#ifdef TARGET_SPARC64
    tcg_gen_trunc_tl_i32(cpu_fprs, src);
    dc->fprs_dirty = 0;
    dc->base.is_jmp = DISAS_EXIT;
#else
    qemu_build_not_reached();
#endif
}

TRANS(WRFPRS, 64, do_wr_special, a, true, do_wrfprs)

static void do_wrgsr(DisasContext *dc, TCGv src)
{
    gen_trap_ifnofpu(dc);
    tcg_gen_mov_tl(cpu_gsr, src);
}

TRANS(WRGSR, 64, do_wr_special, a, true, do_wrgsr)

static void do_wrsoftint_set(DisasContext *dc, TCGv src)
{
    gen_helper_set_softint(tcg_env, src);
}

TRANS(WRSOFTINT_SET, 64, do_wr_special, a, supervisor(dc), do_wrsoftint_set)

static void do_wrsoftint_clr(DisasContext *dc, TCGv src)
{
    gen_helper_clear_softint(tcg_env, src);
}

TRANS(WRSOFTINT_CLR, 64, do_wr_special, a, supervisor(dc), do_wrsoftint_clr)

static void do_wrsoftint(DisasContext *dc, TCGv src)
{
    gen_helper_write_softint(tcg_env, src);
}

TRANS(WRSOFTINT, 64, do_wr_special, a, supervisor(dc), do_wrsoftint)

static void do_wrtick_cmpr(DisasContext *dc, TCGv src)
{
    TCGv_ptr r_tickptr = tcg_temp_new_ptr();

    tcg_gen_st_tl(src, tcg_env, env64_field_offsetof(tick_cmpr));
    tcg_gen_ld_ptr(r_tickptr, tcg_env, env64_field_offsetof(tick));
    translator_io_start(&dc->base);
    gen_helper_tick_set_limit(r_tickptr, src);
    /* End TB to handle timer interrupt */
    dc->base.is_jmp = DISAS_EXIT;
}

TRANS(WRTICK_CMPR, 64, do_wr_special, a, supervisor(dc), do_wrtick_cmpr)

static void do_wrstick(DisasContext *dc, TCGv src)
{
#ifdef TARGET_SPARC64
    TCGv_ptr r_tickptr = tcg_temp_new_ptr();

    tcg_gen_ld_ptr(r_tickptr, tcg_env, offsetof(CPUSPARCState, stick));
    translator_io_start(&dc->base);
    gen_helper_tick_set_count(r_tickptr, src);
    /* End TB to handle timer interrupt */
    dc->base.is_jmp = DISAS_EXIT;
#else
    qemu_build_not_reached();
#endif
}

TRANS(WRSTICK, 64, do_wr_special, a, supervisor(dc), do_wrstick)

static void do_wrstick_cmpr(DisasContext *dc, TCGv src)
{
    TCGv_ptr r_tickptr = tcg_temp_new_ptr();

    tcg_gen_st_tl(src, tcg_env, env64_field_offsetof(stick_cmpr));
    tcg_gen_ld_ptr(r_tickptr, tcg_env, env64_field_offsetof(stick));
    translator_io_start(&dc->base);
    gen_helper_tick_set_limit(r_tickptr, src);
    /* End TB to handle timer interrupt */
    dc->base.is_jmp = DISAS_EXIT;
}

TRANS(WRSTICK_CMPR, 64, do_wr_special, a, supervisor(dc), do_wrstick_cmpr)

static void do_wrpowerdown(DisasContext *dc, TCGv src)
{
    save_state(dc);
    gen_helper_power_down(tcg_env);
}

TRANS(WRPOWERDOWN, POWERDOWN, do_wr_special, a, supervisor(dc), do_wrpowerdown)

static void do_wrpsr(DisasContext *dc, TCGv src)
{
    gen_helper_wrpsr(tcg_env, src);
    tcg_gen_movi_i32(cpu_cc_op, CC_OP_FLAGS);
    dc->cc_op = CC_OP_FLAGS;
    dc->base.is_jmp = DISAS_EXIT;
}

TRANS(WRPSR, 32, do_wr_special, a, supervisor(dc), do_wrpsr)

static void do_wrwim(DisasContext *dc, TCGv src)
{
    target_ulong mask = MAKE_64BIT_MASK(0, dc->def->nwindows);
    TCGv tmp = tcg_temp_new();

    tcg_gen_andi_tl(tmp, src, mask);
    tcg_gen_st_tl(tmp, tcg_env, env32_field_offsetof(wim));
}

TRANS(WRWIM, 32, do_wr_special, a, supervisor(dc), do_wrwim)

static void do_wrtpc(DisasContext *dc, TCGv src)
{
#ifdef TARGET_SPARC64
    TCGv_ptr r_tsptr = tcg_temp_new_ptr();

    gen_load_trap_state_at_tl(r_tsptr);
    tcg_gen_st_tl(src, r_tsptr, offsetof(trap_state, tpc));
#else
    qemu_build_not_reached();
#endif
}

TRANS(WRPR_tpc, 64, do_wr_special, a, supervisor(dc), do_wrtpc)

static void do_wrtnpc(DisasContext *dc, TCGv src)
{
#ifdef TARGET_SPARC64
    TCGv_ptr r_tsptr = tcg_temp_new_ptr();

    gen_load_trap_state_at_tl(r_tsptr);
    tcg_gen_st_tl(src, r_tsptr, offsetof(trap_state, tnpc));
#else
    qemu_build_not_reached();
#endif
}

TRANS(WRPR_tnpc, 64, do_wr_special, a, supervisor(dc), do_wrtnpc)

static void do_wrtstate(DisasContext *dc, TCGv src)
{
#ifdef TARGET_SPARC64
    TCGv_ptr r_tsptr = tcg_temp_new_ptr();

    gen_load_trap_state_at_tl(r_tsptr);
    tcg_gen_st_tl(src, r_tsptr, offsetof(trap_state, tstate));
#else
    qemu_build_not_reached();
#endif
}

TRANS(WRPR_tstate, 64, do_wr_special, a, supervisor(dc), do_wrtstate)

static void do_wrtt(DisasContext *dc, TCGv src)
{
#ifdef TARGET_SPARC64
    TCGv_ptr r_tsptr = tcg_temp_new_ptr();

    gen_load_trap_state_at_tl(r_tsptr);
    tcg_gen_st32_tl(src, r_tsptr, offsetof(trap_state, tt));
#else
    qemu_build_not_reached();
#endif
}

TRANS(WRPR_tt, 64, do_wr_special, a, supervisor(dc), do_wrtt)

static void do_wrtick(DisasContext *dc, TCGv src)
{
    TCGv_ptr r_tickptr = tcg_temp_new_ptr();

    tcg_gen_ld_ptr(r_tickptr, tcg_env, env64_field_offsetof(tick));
    translator_io_start(&dc->base);
    gen_helper_tick_set_count(r_tickptr, src);
    /* End TB to handle timer interrupt */
    dc->base.is_jmp = DISAS_EXIT;
}

TRANS(WRPR_tick, 64, do_wr_special, a, supervisor(dc), do_wrtick)

static void do_wrtba(DisasContext *dc, TCGv src)
{
    tcg_gen_mov_tl(cpu_tbr, src);
}

TRANS(WRPR_tba, 64, do_wr_special, a, supervisor(dc), do_wrtba)

static void do_wrpstate(DisasContext *dc, TCGv src)
{
    save_state(dc);
    if (translator_io_start(&dc->base)) {
        dc->base.is_jmp = DISAS_EXIT;
    }
    gen_helper_wrpstate(tcg_env, src);
    dc->npc = DYNAMIC_PC;
}

TRANS(WRPR_pstate, 64, do_wr_special, a, supervisor(dc), do_wrpstate)

static void do_wrtl(DisasContext *dc, TCGv src)
{
    save_state(dc);
    tcg_gen_st32_tl(src, tcg_env, env64_field_offsetof(tl));
    dc->npc = DYNAMIC_PC;
}

TRANS(WRPR_tl, 64, do_wr_special, a, supervisor(dc), do_wrtl)

static void do_wrpil(DisasContext *dc, TCGv src)
{
    if (translator_io_start(&dc->base)) {
        dc->base.is_jmp = DISAS_EXIT;
    }
    gen_helper_wrpil(tcg_env, src);
}

TRANS(WRPR_pil, 64, do_wr_special, a, supervisor(dc), do_wrpil)

static void do_wrcwp(DisasContext *dc, TCGv src)
{
    gen_helper_wrcwp(tcg_env, src);
}

TRANS(WRPR_cwp, 64, do_wr_special, a, supervisor(dc), do_wrcwp)

static void do_wrcansave(DisasContext *dc, TCGv src)
{
    tcg_gen_st32_tl(src, tcg_env, env64_field_offsetof(cansave));
}

TRANS(WRPR_cansave, 64, do_wr_special, a, supervisor(dc), do_wrcansave)

static void do_wrcanrestore(DisasContext *dc, TCGv src)
{
    tcg_gen_st32_tl(src, tcg_env, env64_field_offsetof(canrestore));
}

TRANS(WRPR_canrestore, 64, do_wr_special, a, supervisor(dc), do_wrcanrestore)

static void do_wrcleanwin(DisasContext *dc, TCGv src)
{
    tcg_gen_st32_tl(src, tcg_env, env64_field_offsetof(cleanwin));
}

TRANS(WRPR_cleanwin, 64, do_wr_special, a, supervisor(dc), do_wrcleanwin)

static void do_wrotherwin(DisasContext *dc, TCGv src)
{
    tcg_gen_st32_tl(src, tcg_env, env64_field_offsetof(otherwin));
}

TRANS(WRPR_otherwin, 64, do_wr_special, a, supervisor(dc), do_wrotherwin)

static void do_wrwstate(DisasContext *dc, TCGv src)
{
    tcg_gen_st32_tl(src, tcg_env, env64_field_offsetof(wstate));
}

TRANS(WRPR_wstate, 64, do_wr_special, a, supervisor(dc), do_wrwstate)

static void do_wrgl(DisasContext *dc, TCGv src)
{
    gen_helper_wrgl(tcg_env, src);
}

TRANS(WRPR_gl, GL, do_wr_special, a, supervisor(dc), do_wrgl)

/* UA2005 strand status */
static void do_wrssr(DisasContext *dc, TCGv src)
{
    tcg_gen_st_tl(src, tcg_env, env64_field_offsetof(ssr));
}

TRANS(WRPR_strand_status, HYPV, do_wr_special, a, hypervisor(dc), do_wrssr)

TRANS(WRTBR, 32, do_wr_special, a, supervisor(dc), do_wrtba)

static void do_wrhpstate(DisasContext *dc, TCGv src)
{
    tcg_gen_st_tl(src, tcg_env, env64_field_offsetof(hpstate));
    dc->base.is_jmp = DISAS_EXIT;
}

TRANS(WRHPR_hpstate, HYPV, do_wr_special, a, hypervisor(dc), do_wrhpstate)

static void do_wrhtstate(DisasContext *dc, TCGv src)
{
    TCGv_i32 tl = tcg_temp_new_i32();
    TCGv_ptr tp = tcg_temp_new_ptr();

    tcg_gen_ld_i32(tl, tcg_env, env64_field_offsetof(tl));
    tcg_gen_andi_i32(tl, tl, MAXTL_MASK);
    tcg_gen_shli_i32(tl, tl, 3);
    tcg_gen_ext_i32_ptr(tp, tl);
    tcg_gen_add_ptr(tp, tp, tcg_env);

    tcg_gen_st_tl(src, tp, env64_field_offsetof(htstate));
}

TRANS(WRHPR_htstate, HYPV, do_wr_special, a, hypervisor(dc), do_wrhtstate)

static void do_wrhintp(DisasContext *dc, TCGv src)
{
    tcg_gen_st_tl(src, tcg_env, env64_field_offsetof(hintp));
}

TRANS(WRHPR_hintp, HYPV, do_wr_special, a, hypervisor(dc), do_wrhintp)

static void do_wrhtba(DisasContext *dc, TCGv src)
{
    tcg_gen_st_tl(src, tcg_env, env64_field_offsetof(htba));
}

TRANS(WRHPR_htba, HYPV, do_wr_special, a, hypervisor(dc), do_wrhtba)

static void do_wrhstick_cmpr(DisasContext *dc, TCGv src)
{
    TCGv_ptr r_tickptr = tcg_temp_new_ptr();

    tcg_gen_st_tl(src, tcg_env, env64_field_offsetof(hstick_cmpr));
    tcg_gen_ld_ptr(r_tickptr, tcg_env, env64_field_offsetof(hstick));
    translator_io_start(&dc->base);
    gen_helper_tick_set_limit(r_tickptr, src);
    /* End TB to handle timer interrupt */
    dc->base.is_jmp = DISAS_EXIT;
}

TRANS(WRHPR_hstick_cmpr, HYPV, do_wr_special, a, hypervisor(dc),
      do_wrhstick_cmpr)

static bool do_saved_restored(DisasContext *dc, bool saved)
{
    if (!supervisor(dc)) {
        return raise_priv(dc);
    }
    if (saved) {
        gen_helper_saved(tcg_env);
    } else {
        gen_helper_restored(tcg_env);
    }
    return advance_pc(dc);
}

TRANS(SAVED, 64, do_saved_restored, true)
TRANS(RESTORED, 64, do_saved_restored, false)

static bool trans_NOP(DisasContext *dc, arg_NOP *a)
{
    return advance_pc(dc);
}

static bool trans_NOP_v7(DisasContext *dc, arg_NOP_v7 *a)
{
    /*
     * TODO: Need a feature bit for sparcv8.
     * In the meantime, treat all 32-bit cpus like sparcv7.
     */
    if (avail_32(dc)) {
        return advance_pc(dc);
    }
    return false;
}

static bool do_arith_int(DisasContext *dc, arg_r_r_ri_cc *a, int cc_op,
                         void (*func)(TCGv, TCGv, TCGv),
                         void (*funci)(TCGv, TCGv, target_long))
{
    TCGv dst, src1;

    /* For simplicity, we under-decoded the rs2 form. */
    if (!a->imm && a->rs2_or_imm & ~0x1f) {
        return false;
    }

    if (a->cc) {
        dst = cpu_cc_dst;
    } else {
        dst = gen_dest_gpr(dc, a->rd);
    }
    src1 = gen_load_gpr(dc, a->rs1);

    if (a->imm || a->rs2_or_imm == 0) {
        if (funci) {
            funci(dst, src1, a->rs2_or_imm);
        } else {
            func(dst, src1, tcg_constant_tl(a->rs2_or_imm));
        }
    } else {
        func(dst, src1, cpu_regs[a->rs2_or_imm]);
    }
    gen_store_gpr(dc, a->rd, dst);

    if (a->cc) {
        tcg_gen_movi_i32(cpu_cc_op, cc_op);
        dc->cc_op = cc_op;
    }
    return advance_pc(dc);
}

static bool do_arith(DisasContext *dc, arg_r_r_ri_cc *a, int cc_op,
                     void (*func)(TCGv, TCGv, TCGv),
                     void (*funci)(TCGv, TCGv, target_long),
                     void (*func_cc)(TCGv, TCGv, TCGv))
{
    if (a->cc) {
        assert(cc_op >= 0);
        return do_arith_int(dc, a, cc_op, func_cc, NULL);
    }
    return do_arith_int(dc, a, cc_op, func, funci);
}

static bool do_logic(DisasContext *dc, arg_r_r_ri_cc *a,
                     void (*func)(TCGv, TCGv, TCGv),
                     void (*funci)(TCGv, TCGv, target_long))
{
    return do_arith_int(dc, a, CC_OP_LOGIC, func, funci);
}

TRANS(AND, ALL, do_logic, a, tcg_gen_and_tl, tcg_gen_andi_tl)
TRANS(XOR, ALL, do_logic, a, tcg_gen_xor_tl, tcg_gen_xori_tl)
TRANS(ANDN, ALL, do_logic, a, tcg_gen_andc_tl, NULL)
TRANS(ORN, ALL, do_logic, a, tcg_gen_orc_tl, NULL)
TRANS(XORN, ALL, do_logic, a, tcg_gen_eqv_tl, NULL)
TRANS(MULX, 64, do_arith, a, -1, tcg_gen_mul_tl, tcg_gen_muli_tl, NULL)
TRANS(UMUL, MUL, do_logic, a, gen_op_umul, NULL)
TRANS(SMUL, MUL, do_logic, a, gen_op_smul, NULL)
TRANS(UDIVX, 64, do_arith, a, -1, gen_op_udivx, NULL, NULL)
TRANS(SDIVX, 64, do_arith, a, -1, gen_op_sdivx, NULL, NULL)
TRANS(UDIV, DIV, do_arith, a, CC_OP_DIV, gen_op_udiv, NULL, gen_op_udivcc)
TRANS(SDIV, DIV, do_arith, a, CC_OP_DIV, gen_op_sdiv, NULL, gen_op_sdivcc)
TRANS(TADDcc, ALL, do_arith, a, CC_OP_TADD, NULL, NULL, gen_op_add_cc)
TRANS(TSUBcc, ALL, do_arith, a, CC_OP_TSUB, NULL, NULL, gen_op_sub_cc)
TRANS(TADDccTV, ALL, do_arith, a, CC_OP_TADDTV, NULL, NULL, gen_op_taddcctv)
TRANS(TSUBccTV, ALL, do_arith, a, CC_OP_TSUBTV, NULL, NULL, gen_op_tsubcctv)

/* TODO: Should have feature bit -- comes in with UltraSparc T2. */
TRANS(POPC, 64, do_arith, a, -1, gen_op_popc, NULL, NULL)

static bool trans_OR(DisasContext *dc, arg_r_r_ri_cc *a)
{
    /* OR with %g0 is the canonical alias for MOV. */
    if (!a->cc && a->rs1 == 0) {
        if (a->imm || a->rs2_or_imm == 0) {
            gen_store_gpr(dc, a->rd, tcg_constant_tl(a->rs2_or_imm));
        } else if (a->rs2_or_imm & ~0x1f) {
            /* For simplicity, we under-decoded the rs2 form. */
            return false;
        } else {
            gen_store_gpr(dc, a->rd, cpu_regs[a->rs2_or_imm]);
        }
        return advance_pc(dc);
    }
    return do_logic(dc, a, tcg_gen_or_tl, tcg_gen_ori_tl);
}

TRANS(ADD, ALL, do_arith, a, CC_OP_ADD,
      tcg_gen_add_tl, tcg_gen_addi_tl, gen_op_add_cc)
TRANS(SUB, ALL, do_arith, a, CC_OP_SUB,
      tcg_gen_sub_tl, tcg_gen_subi_tl, gen_op_sub_cc)

static bool trans_ADDC(DisasContext *dc, arg_r_r_ri_cc *a)
{
    switch (dc->cc_op) {
    case CC_OP_DIV:
    case CC_OP_LOGIC:
        /* Carry is known to be zero.  Fall back to plain ADD.  */
        return do_arith(dc, a, CC_OP_ADD,
                        tcg_gen_add_tl, tcg_gen_addi_tl, gen_op_add_cc);
    case CC_OP_ADD:
    case CC_OP_TADD:
    case CC_OP_TADDTV:
        return do_arith(dc, a, CC_OP_ADDX,
                        gen_op_addc_add, NULL, gen_op_addccc_add);
    case CC_OP_SUB:
    case CC_OP_TSUB:
    case CC_OP_TSUBTV:
        return do_arith(dc, a, CC_OP_ADDX,
                        gen_op_addc_sub, NULL, gen_op_addccc_sub);
    default:
        return do_arith(dc, a, CC_OP_ADDX,
                        gen_op_addc_generic, NULL, gen_op_addccc_generic);
    }
}

static bool trans_SUBC(DisasContext *dc, arg_r_r_ri_cc *a)
{
    switch (dc->cc_op) {
    case CC_OP_DIV:
    case CC_OP_LOGIC:
        /* Carry is known to be zero.  Fall back to plain SUB.  */
        return do_arith(dc, a, CC_OP_SUB,
                        tcg_gen_sub_tl, tcg_gen_subi_tl, gen_op_sub_cc);
    case CC_OP_ADD:
    case CC_OP_TADD:
    case CC_OP_TADDTV:
        return do_arith(dc, a, CC_OP_SUBX,
                        gen_op_subc_add, NULL, gen_op_subccc_add);
    case CC_OP_SUB:
    case CC_OP_TSUB:
    case CC_OP_TSUBTV:
        return do_arith(dc, a, CC_OP_SUBX,
                        gen_op_subc_sub, NULL, gen_op_subccc_sub);
    default:
        return do_arith(dc, a, CC_OP_SUBX,
                        gen_op_subc_generic, NULL, gen_op_subccc_generic);
    }
}

static bool trans_MULScc(DisasContext *dc, arg_r_r_ri_cc *a)
{
    update_psr(dc);
    return do_arith(dc, a, CC_OP_ADD, NULL, NULL, gen_op_mulscc);
}

static bool do_shift_r(DisasContext *dc, arg_shiftr *a, bool l, bool u)
{
    TCGv dst, src1, src2;

    /* Reject 64-bit shifts for sparc32. */
    if (avail_32(dc) && a->x) {
        return false;
    }

    src2 = tcg_temp_new();
    tcg_gen_andi_tl(src2, gen_load_gpr(dc, a->rs2), a->x ? 63 : 31);
    src1 = gen_load_gpr(dc, a->rs1);
    dst = gen_dest_gpr(dc, a->rd);

    if (l) {
        tcg_gen_shl_tl(dst, src1, src2);
        if (!a->x) {
            tcg_gen_ext32u_tl(dst, dst);
        }
    } else if (u) {
        if (!a->x) {
            tcg_gen_ext32u_tl(dst, src1);
            src1 = dst;
        }
        tcg_gen_shr_tl(dst, src1, src2);
    } else {
        if (!a->x) {
            tcg_gen_ext32s_tl(dst, src1);
            src1 = dst;
        }
        tcg_gen_sar_tl(dst, src1, src2);
    }
    gen_store_gpr(dc, a->rd, dst);
    return advance_pc(dc);
}

TRANS(SLL_r, ALL, do_shift_r, a, true, true)
TRANS(SRL_r, ALL, do_shift_r, a, false, true)
TRANS(SRA_r, ALL, do_shift_r, a, false, false)

static bool do_shift_i(DisasContext *dc, arg_shifti *a, bool l, bool u)
{
    TCGv dst, src1;

    /* Reject 64-bit shifts for sparc32. */
    if (avail_32(dc) && (a->x || a->i >= 32)) {
        return false;
    }

    src1 = gen_load_gpr(dc, a->rs1);
    dst = gen_dest_gpr(dc, a->rd);

    if (avail_32(dc) || a->x) {
        if (l) {
            tcg_gen_shli_tl(dst, src1, a->i);
        } else if (u) {
            tcg_gen_shri_tl(dst, src1, a->i);
        } else {
            tcg_gen_sari_tl(dst, src1, a->i);
        }
    } else {
        if (l) {
            tcg_gen_deposit_z_tl(dst, src1, a->i, 32 - a->i);
        } else if (u) {
            tcg_gen_extract_tl(dst, src1, a->i, 32 - a->i);
        } else {
            tcg_gen_sextract_tl(dst, src1, a->i, 32 - a->i);
        }
    }
    gen_store_gpr(dc, a->rd, dst);
    return advance_pc(dc);
}

TRANS(SLL_i, ALL, do_shift_i, a, true, true)
TRANS(SRL_i, ALL, do_shift_i, a, false, true)
TRANS(SRA_i, ALL, do_shift_i, a, false, false)

static TCGv gen_rs2_or_imm(DisasContext *dc, bool imm, int rs2_or_imm)
{
    /* For simplicity, we under-decoded the rs2 form. */
    if (!imm && rs2_or_imm & ~0x1f) {
        return NULL;
    }
    if (imm || rs2_or_imm == 0) {
        return tcg_constant_tl(rs2_or_imm);
    } else {
        return cpu_regs[rs2_or_imm];
    }
}

static bool do_mov_cond(DisasContext *dc, DisasCompare *cmp, int rd, TCGv src2)
{
    TCGv dst = gen_load_gpr(dc, rd);

    tcg_gen_movcond_tl(cmp->cond, dst, cmp->c1, cmp->c2, src2, dst);
    gen_store_gpr(dc, rd, dst);
    return advance_pc(dc);
}

static bool trans_MOVcc(DisasContext *dc, arg_MOVcc *a)
{
    TCGv src2 = gen_rs2_or_imm(dc, a->imm, a->rs2_or_imm);
    DisasCompare cmp;

    if (src2 == NULL) {
        return false;
    }
    gen_compare(&cmp, a->cc, a->cond, dc);
    return do_mov_cond(dc, &cmp, a->rd, src2);
}

static bool trans_MOVfcc(DisasContext *dc, arg_MOVfcc *a)
{
    TCGv src2 = gen_rs2_or_imm(dc, a->imm, a->rs2_or_imm);
    DisasCompare cmp;

    if (src2 == NULL) {
        return false;
    }
    gen_fcompare(&cmp, a->cc, a->cond);
    return do_mov_cond(dc, &cmp, a->rd, src2);
}

static bool trans_MOVR(DisasContext *dc, arg_MOVR *a)
{
    TCGv src2 = gen_rs2_or_imm(dc, a->imm, a->rs2_or_imm);
    DisasCompare cmp;

    if (src2 == NULL) {
        return false;
    }
    gen_compare_reg(&cmp, a->cond, gen_load_gpr(dc, a->rs1));
    return do_mov_cond(dc, &cmp, a->rd, src2);
}

static bool do_add_special(DisasContext *dc, arg_r_r_ri *a,
                           bool (*func)(DisasContext *dc, int rd, TCGv src))
{
    TCGv src1, sum;

    /* For simplicity, we under-decoded the rs2 form. */
    if (!a->imm && a->rs2_or_imm & ~0x1f) {
        return false;
    }

    /*
     * Always load the sum into a new temporary.
     * This is required to capture the value across a window change,
     * e.g. SAVE and RESTORE, and may be optimized away otherwise.
     */
    sum = tcg_temp_new();
    src1 = gen_load_gpr(dc, a->rs1);
    if (a->imm || a->rs2_or_imm == 0) {
        tcg_gen_addi_tl(sum, src1, a->rs2_or_imm);
    } else {
        tcg_gen_add_tl(sum, src1, cpu_regs[a->rs2_or_imm]);
    }
    return func(dc, a->rd, sum);
}

static bool do_jmpl(DisasContext *dc, int rd, TCGv src)
{
    /*
     * Preserve pc across advance, so that we can delay
     * the writeback to rd until after src is consumed.
     */
    target_ulong cur_pc = dc->pc;

    gen_check_align(dc, src, 3);

    gen_mov_pc_npc(dc);
    tcg_gen_mov_tl(cpu_npc, src);
    gen_address_mask(dc, cpu_npc);
    gen_store_gpr(dc, rd, tcg_constant_tl(cur_pc));

    dc->npc = DYNAMIC_PC_LOOKUP;
    return true;
}

TRANS(JMPL, ALL, do_add_special, a, do_jmpl)

static bool do_rett(DisasContext *dc, int rd, TCGv src)
{
    if (!supervisor(dc)) {
        return raise_priv(dc);
    }

    gen_check_align(dc, src, 3);

    gen_mov_pc_npc(dc);
    tcg_gen_mov_tl(cpu_npc, src);
    gen_helper_rett(tcg_env);

    dc->npc = DYNAMIC_PC;
    return true;
}

TRANS(RETT, 32, do_add_special, a, do_rett)

static bool do_return(DisasContext *dc, int rd, TCGv src)
{
    gen_check_align(dc, src, 3);

    gen_mov_pc_npc(dc);
    tcg_gen_mov_tl(cpu_npc, src);
    gen_address_mask(dc, cpu_npc);

    gen_helper_restore(tcg_env);
    dc->npc = DYNAMIC_PC_LOOKUP;
    return true;
}

TRANS(RETURN, 64, do_add_special, a, do_return)

static bool do_save(DisasContext *dc, int rd, TCGv src)
{
    gen_helper_save(tcg_env);
    gen_store_gpr(dc, rd, src);
    return advance_pc(dc);
}

TRANS(SAVE, ALL, do_add_special, a, do_save)

static bool do_restore(DisasContext *dc, int rd, TCGv src)
{
    gen_helper_restore(tcg_env);
    gen_store_gpr(dc, rd, src);
    return advance_pc(dc);
}

TRANS(RESTORE, ALL, do_add_special, a, do_restore)

static bool do_done_retry(DisasContext *dc, bool done)
{
    if (!supervisor(dc)) {
        return raise_priv(dc);
    }
    dc->npc = DYNAMIC_PC;
    dc->pc = DYNAMIC_PC;
    translator_io_start(&dc->base);
    if (done) {
        gen_helper_done(tcg_env);
    } else {
        gen_helper_retry(tcg_env);
    }
    return true;
}

TRANS(DONE, 64, do_done_retry, true)
TRANS(RETRY, 64, do_done_retry, false)

#define CHECK_IU_FEATURE(dc, FEATURE)                      \
    if (!((dc)->def->features & CPU_FEATURE_ ## FEATURE))  \
        goto illegal_insn;
#define CHECK_FPU_FEATURE(dc, FEATURE)                     \
    if (!((dc)->def->features & CPU_FEATURE_ ## FEATURE))  \
        goto nfpu_insn;

/* before an instruction, dc->pc must be static */
static void disas_sparc_legacy(DisasContext *dc, unsigned int insn)
{
    unsigned int opc, rs1, rs2, rd;
    TCGv cpu_src1;
    TCGv cpu_src2 __attribute__((unused));
    TCGv_i32 cpu_src1_32, cpu_src2_32, cpu_dst_32;
    TCGv_i64 cpu_src1_64, cpu_src2_64, cpu_dst_64;
    target_long simm;

    opc = GET_FIELD(insn, 0, 1);
    rd = GET_FIELD(insn, 2, 6);

    switch (opc) {
    case 0:
        goto illegal_insn; /* in decodetree */
    case 1:
        g_assert_not_reached(); /* in decodetree */
    case 2:                     /* FPU & Logical Operations */
        {
            unsigned int xop = GET_FIELD(insn, 7, 12);
            TCGv cpu_dst __attribute__((unused)) = tcg_temp_new();

            if (xop == 0x34) {   /* FPU Operations */
                if (gen_trap_ifnofpu(dc)) {
                    goto jmp_insn;
                }
                gen_op_clear_ieee_excp_and_FTT();
                rs1 = GET_FIELD(insn, 13, 17);
                rs2 = GET_FIELD(insn, 27, 31);
                xop = GET_FIELD(insn, 18, 26);

                switch (xop) {
                case 0x1: /* fmovs */
                    cpu_src1_32 = gen_load_fpr_F(dc, rs2);
                    gen_store_fpr_F(dc, rd, cpu_src1_32);
                    break;
                case 0x5: /* fnegs */
                    gen_ne_fop_FF(dc, rd, rs2, gen_helper_fnegs);
                    break;
                case 0x9: /* fabss */
                    gen_ne_fop_FF(dc, rd, rs2, gen_helper_fabss);
                    break;
                case 0x29: /* fsqrts */
                    gen_fop_FF(dc, rd, rs2, gen_helper_fsqrts);
                    break;
                case 0x2a: /* fsqrtd */
                    gen_fop_DD(dc, rd, rs2, gen_helper_fsqrtd);
                    break;
                case 0x2b: /* fsqrtq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_fop_QQ(dc, rd, rs2, gen_helper_fsqrtq);
                    break;
                case 0x41: /* fadds */
                    gen_fop_FFF(dc, rd, rs1, rs2, gen_helper_fadds);
                    break;
                case 0x42: /* faddd */
                    gen_fop_DDD(dc, rd, rs1, rs2, gen_helper_faddd);
                    break;
                case 0x43: /* faddq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_fop_QQQ(dc, rd, rs1, rs2, gen_helper_faddq);
                    break;
                case 0x45: /* fsubs */
                    gen_fop_FFF(dc, rd, rs1, rs2, gen_helper_fsubs);
                    break;
                case 0x46: /* fsubd */
                    gen_fop_DDD(dc, rd, rs1, rs2, gen_helper_fsubd);
                    break;
                case 0x47: /* fsubq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_fop_QQQ(dc, rd, rs1, rs2, gen_helper_fsubq);
                    break;
                case 0x49: /* fmuls */
                    gen_fop_FFF(dc, rd, rs1, rs2, gen_helper_fmuls);
                    break;
                case 0x4a: /* fmuld */
                    gen_fop_DDD(dc, rd, rs1, rs2, gen_helper_fmuld);
                    break;
                case 0x4b: /* fmulq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_fop_QQQ(dc, rd, rs1, rs2, gen_helper_fmulq);
                    break;
                case 0x4d: /* fdivs */
                    gen_fop_FFF(dc, rd, rs1, rs2, gen_helper_fdivs);
                    break;
                case 0x4e: /* fdivd */
                    gen_fop_DDD(dc, rd, rs1, rs2, gen_helper_fdivd);
                    break;
                case 0x4f: /* fdivq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_fop_QQQ(dc, rd, rs1, rs2, gen_helper_fdivq);
                    break;
                case 0x69: /* fsmuld */
                    CHECK_FPU_FEATURE(dc, FSMULD);
                    gen_fop_DFF(dc, rd, rs1, rs2, gen_helper_fsmuld);
                    break;
                case 0x6e: /* fdmulq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_fop_QDD(dc, rd, rs1, rs2, gen_helper_fdmulq);
                    break;
                case 0xc4: /* fitos */
                    gen_fop_FF(dc, rd, rs2, gen_helper_fitos);
                    break;
                case 0xc6: /* fdtos */
                    gen_fop_FD(dc, rd, rs2, gen_helper_fdtos);
                    break;
                case 0xc7: /* fqtos */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_fop_FQ(dc, rd, rs2, gen_helper_fqtos);
                    break;
                case 0xc8: /* fitod */
                    gen_ne_fop_DF(dc, rd, rs2, gen_helper_fitod);
                    break;
                case 0xc9: /* fstod */
                    gen_ne_fop_DF(dc, rd, rs2, gen_helper_fstod);
                    break;
                case 0xcb: /* fqtod */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_fop_DQ(dc, rd, rs2, gen_helper_fqtod);
                    break;
                case 0xcc: /* fitoq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_ne_fop_QF(dc, rd, rs2, gen_helper_fitoq);
                    break;
                case 0xcd: /* fstoq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_ne_fop_QF(dc, rd, rs2, gen_helper_fstoq);
                    break;
                case 0xce: /* fdtoq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_ne_fop_QD(dc, rd, rs2, gen_helper_fdtoq);
                    break;
                case 0xd1: /* fstoi */
                    gen_fop_FF(dc, rd, rs2, gen_helper_fstoi);
                    break;
                case 0xd2: /* fdtoi */
                    gen_fop_FD(dc, rd, rs2, gen_helper_fdtoi);
                    break;
                case 0xd3: /* fqtoi */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_fop_FQ(dc, rd, rs2, gen_helper_fqtoi);
                    break;
#ifdef TARGET_SPARC64
                case 0x2: /* V9 fmovd */
                    cpu_src1_64 = gen_load_fpr_D(dc, rs2);
                    gen_store_fpr_D(dc, rd, cpu_src1_64);
                    break;
                case 0x3: /* V9 fmovq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_move_Q(dc, rd, rs2);
                    break;
                case 0x6: /* V9 fnegd */
                    gen_ne_fop_DD(dc, rd, rs2, gen_helper_fnegd);
                    break;
                case 0x7: /* V9 fnegq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_ne_fop_QQ(dc, rd, rs2, gen_helper_fnegq);
                    break;
                case 0xa: /* V9 fabsd */
                    gen_ne_fop_DD(dc, rd, rs2, gen_helper_fabsd);
                    break;
                case 0xb: /* V9 fabsq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_ne_fop_QQ(dc, rd, rs2, gen_helper_fabsq);
                    break;
                case 0x81: /* V9 fstox */
                    gen_fop_DF(dc, rd, rs2, gen_helper_fstox);
                    break;
                case 0x82: /* V9 fdtox */
                    gen_fop_DD(dc, rd, rs2, gen_helper_fdtox);
                    break;
                case 0x83: /* V9 fqtox */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_fop_DQ(dc, rd, rs2, gen_helper_fqtox);
                    break;
                case 0x84: /* V9 fxtos */
                    gen_fop_FD(dc, rd, rs2, gen_helper_fxtos);
                    break;
                case 0x88: /* V9 fxtod */
                    gen_fop_DD(dc, rd, rs2, gen_helper_fxtod);
                    break;
                case 0x8c: /* V9 fxtoq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_ne_fop_QD(dc, rd, rs2, gen_helper_fxtoq);
                    break;
#endif
                default:
                    goto illegal_insn;
                }
            } else if (xop == 0x35) {   /* FPU Operations */
#ifdef TARGET_SPARC64
                int cond;
#endif
                if (gen_trap_ifnofpu(dc)) {
                    goto jmp_insn;
                }
                gen_op_clear_ieee_excp_and_FTT();
                rs1 = GET_FIELD(insn, 13, 17);
                rs2 = GET_FIELD(insn, 27, 31);
                xop = GET_FIELD(insn, 18, 26);

#ifdef TARGET_SPARC64
#define FMOVR(sz)                                                  \
                do {                                               \
                    DisasCompare cmp;                              \
                    cond = GET_FIELD_SP(insn, 10, 12);             \
                    cpu_src1 = get_src1(dc, insn);                 \
                    gen_compare_reg(&cmp, cond, cpu_src1);         \
                    gen_fmov##sz(dc, &cmp, rd, rs2);               \
                } while (0)

                if ((xop & 0x11f) == 0x005) { /* V9 fmovsr */
                    FMOVR(s);
                    break;
                } else if ((xop & 0x11f) == 0x006) { // V9 fmovdr
                    FMOVR(d);
                    break;
                } else if ((xop & 0x11f) == 0x007) { // V9 fmovqr
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    FMOVR(q);
                    break;
                }
#undef FMOVR
#endif
                switch (xop) {
#ifdef TARGET_SPARC64
#define FMOVCC(fcc, sz)                                                 \
                    do {                                                \
                        DisasCompare cmp;                               \
                        cond = GET_FIELD_SP(insn, 14, 17);              \
                        gen_fcompare(&cmp, fcc, cond);                  \
                        gen_fmov##sz(dc, &cmp, rd, rs2);                \
                    } while (0)

                    case 0x001: /* V9 fmovscc %fcc0 */
                        FMOVCC(0, s);
                        break;
                    case 0x002: /* V9 fmovdcc %fcc0 */
                        FMOVCC(0, d);
                        break;
                    case 0x003: /* V9 fmovqcc %fcc0 */
                        CHECK_FPU_FEATURE(dc, FLOAT128);
                        FMOVCC(0, q);
                        break;
                    case 0x041: /* V9 fmovscc %fcc1 */
                        FMOVCC(1, s);
                        break;
                    case 0x042: /* V9 fmovdcc %fcc1 */
                        FMOVCC(1, d);
                        break;
                    case 0x043: /* V9 fmovqcc %fcc1 */
                        CHECK_FPU_FEATURE(dc, FLOAT128);
                        FMOVCC(1, q);
                        break;
                    case 0x081: /* V9 fmovscc %fcc2 */
                        FMOVCC(2, s);
                        break;
                    case 0x082: /* V9 fmovdcc %fcc2 */
                        FMOVCC(2, d);
                        break;
                    case 0x083: /* V9 fmovqcc %fcc2 */
                        CHECK_FPU_FEATURE(dc, FLOAT128);
                        FMOVCC(2, q);
                        break;
                    case 0x0c1: /* V9 fmovscc %fcc3 */
                        FMOVCC(3, s);
                        break;
                    case 0x0c2: /* V9 fmovdcc %fcc3 */
                        FMOVCC(3, d);
                        break;
                    case 0x0c3: /* V9 fmovqcc %fcc3 */
                        CHECK_FPU_FEATURE(dc, FLOAT128);
                        FMOVCC(3, q);
                        break;
#undef FMOVCC
#define FMOVCC(xcc, sz)                                                 \
                    do {                                                \
                        DisasCompare cmp;                               \
                        cond = GET_FIELD_SP(insn, 14, 17);              \
                        gen_compare(&cmp, xcc, cond, dc);               \
                        gen_fmov##sz(dc, &cmp, rd, rs2);                \
                    } while (0)

                    case 0x101: /* V9 fmovscc %icc */
                        FMOVCC(0, s);
                        break;
                    case 0x102: /* V9 fmovdcc %icc */
                        FMOVCC(0, d);
                        break;
                    case 0x103: /* V9 fmovqcc %icc */
                        CHECK_FPU_FEATURE(dc, FLOAT128);
                        FMOVCC(0, q);
                        break;
                    case 0x181: /* V9 fmovscc %xcc */
                        FMOVCC(1, s);
                        break;
                    case 0x182: /* V9 fmovdcc %xcc */
                        FMOVCC(1, d);
                        break;
                    case 0x183: /* V9 fmovqcc %xcc */
                        CHECK_FPU_FEATURE(dc, FLOAT128);
                        FMOVCC(1, q);
                        break;
#undef FMOVCC
#endif
                    case 0x51: /* fcmps, V9 %fcc */
                        cpu_src1_32 = gen_load_fpr_F(dc, rs1);
                        cpu_src2_32 = gen_load_fpr_F(dc, rs2);
                        gen_op_fcmps(rd & 3, cpu_src1_32, cpu_src2_32);
                        break;
                    case 0x52: /* fcmpd, V9 %fcc */
                        cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                        cpu_src2_64 = gen_load_fpr_D(dc, rs2);
                        gen_op_fcmpd(rd & 3, cpu_src1_64, cpu_src2_64);
                        break;
                    case 0x53: /* fcmpq, V9 %fcc */
                        CHECK_FPU_FEATURE(dc, FLOAT128);
                        gen_op_load_fpr_QT0(QFPREG(rs1));
                        gen_op_load_fpr_QT1(QFPREG(rs2));
                        gen_op_fcmpq(rd & 3);
                        break;
                    case 0x55: /* fcmpes, V9 %fcc */
                        cpu_src1_32 = gen_load_fpr_F(dc, rs1);
                        cpu_src2_32 = gen_load_fpr_F(dc, rs2);
                        gen_op_fcmpes(rd & 3, cpu_src1_32, cpu_src2_32);
                        break;
                    case 0x56: /* fcmped, V9 %fcc */
                        cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                        cpu_src2_64 = gen_load_fpr_D(dc, rs2);
                        gen_op_fcmped(rd & 3, cpu_src1_64, cpu_src2_64);
                        break;
                    case 0x57: /* fcmpeq, V9 %fcc */
                        CHECK_FPU_FEATURE(dc, FLOAT128);
                        gen_op_load_fpr_QT0(QFPREG(rs1));
                        gen_op_load_fpr_QT1(QFPREG(rs2));
                        gen_op_fcmpeq(rd & 3);
                        break;
                    default:
                        goto illegal_insn;
                }
            } else if (xop == 0x36) {
#ifdef TARGET_SPARC64
                /* VIS */
                int opf = GET_FIELD_SP(insn, 5, 13);
                rs1 = GET_FIELD(insn, 13, 17);
                rs2 = GET_FIELD(insn, 27, 31);
                if (gen_trap_ifnofpu(dc)) {
                    goto jmp_insn;
                }

                switch (opf) {
                case 0x000: /* VIS I edge8cc */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 8, 1, 0);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x001: /* VIS II edge8n */
                    CHECK_FPU_FEATURE(dc, VIS2);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 8, 0, 0);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x002: /* VIS I edge8lcc */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 8, 1, 1);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x003: /* VIS II edge8ln */
                    CHECK_FPU_FEATURE(dc, VIS2);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 8, 0, 1);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x004: /* VIS I edge16cc */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 16, 1, 0);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x005: /* VIS II edge16n */
                    CHECK_FPU_FEATURE(dc, VIS2);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 16, 0, 0);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x006: /* VIS I edge16lcc */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 16, 1, 1);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x007: /* VIS II edge16ln */
                    CHECK_FPU_FEATURE(dc, VIS2);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 16, 0, 1);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x008: /* VIS I edge32cc */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 32, 1, 0);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x009: /* VIS II edge32n */
                    CHECK_FPU_FEATURE(dc, VIS2);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 32, 0, 0);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x00a: /* VIS I edge32lcc */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 32, 1, 1);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x00b: /* VIS II edge32ln */
                    CHECK_FPU_FEATURE(dc, VIS2);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 32, 0, 1);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x010: /* VIS I array8 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_helper_array8(cpu_dst, cpu_src1, cpu_src2);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x012: /* VIS I array16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_helper_array8(cpu_dst, cpu_src1, cpu_src2);
                    tcg_gen_shli_i64(cpu_dst, cpu_dst, 1);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x014: /* VIS I array32 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_helper_array8(cpu_dst, cpu_src1, cpu_src2);
                    tcg_gen_shli_i64(cpu_dst, cpu_dst, 2);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x018: /* VIS I alignaddr */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_alignaddr(cpu_dst, cpu_src1, cpu_src2, 0);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x01a: /* VIS I alignaddrl */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_alignaddr(cpu_dst, cpu_src1, cpu_src2, 1);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x019: /* VIS II bmask */
                    CHECK_FPU_FEATURE(dc, VIS2);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    tcg_gen_add_tl(cpu_dst, cpu_src1, cpu_src2);
                    tcg_gen_deposit_tl(cpu_gsr, cpu_gsr, cpu_dst, 32, 32);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x020: /* VIS I fcmple16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                    cpu_src2_64 = gen_load_fpr_D(dc, rs2);
                    gen_helper_fcmple16(cpu_dst, cpu_src1_64, cpu_src2_64);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x022: /* VIS I fcmpne16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                    cpu_src2_64 = gen_load_fpr_D(dc, rs2);
                    gen_helper_fcmpne16(cpu_dst, cpu_src1_64, cpu_src2_64);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x024: /* VIS I fcmple32 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                    cpu_src2_64 = gen_load_fpr_D(dc, rs2);
                    gen_helper_fcmple32(cpu_dst, cpu_src1_64, cpu_src2_64);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x026: /* VIS I fcmpne32 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                    cpu_src2_64 = gen_load_fpr_D(dc, rs2);
                    gen_helper_fcmpne32(cpu_dst, cpu_src1_64, cpu_src2_64);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x028: /* VIS I fcmpgt16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                    cpu_src2_64 = gen_load_fpr_D(dc, rs2);
                    gen_helper_fcmpgt16(cpu_dst, cpu_src1_64, cpu_src2_64);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x02a: /* VIS I fcmpeq16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                    cpu_src2_64 = gen_load_fpr_D(dc, rs2);
                    gen_helper_fcmpeq16(cpu_dst, cpu_src1_64, cpu_src2_64);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x02c: /* VIS I fcmpgt32 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                    cpu_src2_64 = gen_load_fpr_D(dc, rs2);
                    gen_helper_fcmpgt32(cpu_dst, cpu_src1_64, cpu_src2_64);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x02e: /* VIS I fcmpeq32 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                    cpu_src2_64 = gen_load_fpr_D(dc, rs2);
                    gen_helper_fcmpeq32(cpu_dst, cpu_src1_64, cpu_src2_64);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x031: /* VIS I fmul8x16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fmul8x16);
                    break;
                case 0x033: /* VIS I fmul8x16au */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fmul8x16au);
                    break;
                case 0x035: /* VIS I fmul8x16al */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fmul8x16al);
                    break;
                case 0x036: /* VIS I fmul8sux16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fmul8sux16);
                    break;
                case 0x037: /* VIS I fmul8ulx16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fmul8ulx16);
                    break;
                case 0x038: /* VIS I fmuld8sux16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fmuld8sux16);
                    break;
                case 0x039: /* VIS I fmuld8ulx16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fmuld8ulx16);
                    break;
                case 0x03a: /* VIS I fpack32 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_gsr_fop_DDD(dc, rd, rs1, rs2, gen_helper_fpack32);
                    break;
                case 0x03b: /* VIS I fpack16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs2);
                    cpu_dst_32 = gen_dest_fpr_F(dc);
                    gen_helper_fpack16(cpu_dst_32, cpu_gsr, cpu_src1_64);
                    gen_store_fpr_F(dc, rd, cpu_dst_32);
                    break;
                case 0x03d: /* VIS I fpackfix */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs2);
                    cpu_dst_32 = gen_dest_fpr_F(dc);
                    gen_helper_fpackfix(cpu_dst_32, cpu_gsr, cpu_src1_64);
                    gen_store_fpr_F(dc, rd, cpu_dst_32);
                    break;
                case 0x03e: /* VIS I pdist */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDDD(dc, rd, rs1, rs2, gen_helper_pdist);
                    break;
                case 0x048: /* VIS I faligndata */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_gsr_fop_DDD(dc, rd, rs1, rs2, gen_faligndata);
                    break;
                case 0x04b: /* VIS I fpmerge */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fpmerge);
                    break;
                case 0x04c: /* VIS II bshuffle */
                    CHECK_FPU_FEATURE(dc, VIS2);
                    gen_gsr_fop_DDD(dc, rd, rs1, rs2, gen_helper_bshuffle);
                    break;
                case 0x04d: /* VIS I fexpand */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fexpand);
                    break;
                case 0x050: /* VIS I fpadd16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fpadd16);
                    break;
                case 0x051: /* VIS I fpadd16s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, gen_helper_fpadd16s);
                    break;
                case 0x052: /* VIS I fpadd32 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fpadd32);
                    break;
                case 0x053: /* VIS I fpadd32s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, tcg_gen_add_i32);
                    break;
                case 0x054: /* VIS I fpsub16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fpsub16);
                    break;
                case 0x055: /* VIS I fpsub16s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, gen_helper_fpsub16s);
                    break;
                case 0x056: /* VIS I fpsub32 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fpsub32);
                    break;
                case 0x057: /* VIS I fpsub32s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, tcg_gen_sub_i32);
                    break;
                case 0x060: /* VIS I fzero */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_dst_64 = gen_dest_fpr_D(dc, rd);
                    tcg_gen_movi_i64(cpu_dst_64, 0);
                    gen_store_fpr_D(dc, rd, cpu_dst_64);
                    break;
                case 0x061: /* VIS I fzeros */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_dst_32 = gen_dest_fpr_F(dc);
                    tcg_gen_movi_i32(cpu_dst_32, 0);
                    gen_store_fpr_F(dc, rd, cpu_dst_32);
                    break;
                case 0x062: /* VIS I fnor */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, tcg_gen_nor_i64);
                    break;
                case 0x063: /* VIS I fnors */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, tcg_gen_nor_i32);
                    break;
                case 0x064: /* VIS I fandnot2 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, tcg_gen_andc_i64);
                    break;
                case 0x065: /* VIS I fandnot2s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, tcg_gen_andc_i32);
                    break;
                case 0x066: /* VIS I fnot2 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DD(dc, rd, rs2, tcg_gen_not_i64);
                    break;
                case 0x067: /* VIS I fnot2s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FF(dc, rd, rs2, tcg_gen_not_i32);
                    break;
                case 0x068: /* VIS I fandnot1 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs2, rs1, tcg_gen_andc_i64);
                    break;
                case 0x069: /* VIS I fandnot1s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs2, rs1, tcg_gen_andc_i32);
                    break;
                case 0x06a: /* VIS I fnot1 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DD(dc, rd, rs1, tcg_gen_not_i64);
                    break;
                case 0x06b: /* VIS I fnot1s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FF(dc, rd, rs1, tcg_gen_not_i32);
                    break;
                case 0x06c: /* VIS I fxor */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, tcg_gen_xor_i64);
                    break;
                case 0x06d: /* VIS I fxors */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, tcg_gen_xor_i32);
                    break;
                case 0x06e: /* VIS I fnand */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, tcg_gen_nand_i64);
                    break;
                case 0x06f: /* VIS I fnands */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, tcg_gen_nand_i32);
                    break;
                case 0x070: /* VIS I fand */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, tcg_gen_and_i64);
                    break;
                case 0x071: /* VIS I fands */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, tcg_gen_and_i32);
                    break;
                case 0x072: /* VIS I fxnor */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, tcg_gen_eqv_i64);
                    break;
                case 0x073: /* VIS I fxnors */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, tcg_gen_eqv_i32);
                    break;
                case 0x074: /* VIS I fsrc1 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                    gen_store_fpr_D(dc, rd, cpu_src1_64);
                    break;
                case 0x075: /* VIS I fsrc1s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_32 = gen_load_fpr_F(dc, rs1);
                    gen_store_fpr_F(dc, rd, cpu_src1_32);
                    break;
                case 0x076: /* VIS I fornot2 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, tcg_gen_orc_i64);
                    break;
                case 0x077: /* VIS I fornot2s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, tcg_gen_orc_i32);
                    break;
                case 0x078: /* VIS I fsrc2 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs2);
                    gen_store_fpr_D(dc, rd, cpu_src1_64);
                    break;
                case 0x079: /* VIS I fsrc2s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_32 = gen_load_fpr_F(dc, rs2);
                    gen_store_fpr_F(dc, rd, cpu_src1_32);
                    break;
                case 0x07a: /* VIS I fornot1 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs2, rs1, tcg_gen_orc_i64);
                    break;
                case 0x07b: /* VIS I fornot1s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs2, rs1, tcg_gen_orc_i32);
                    break;
                case 0x07c: /* VIS I for */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, tcg_gen_or_i64);
                    break;
                case 0x07d: /* VIS I fors */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, tcg_gen_or_i32);
                    break;
                case 0x07e: /* VIS I fone */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_dst_64 = gen_dest_fpr_D(dc, rd);
                    tcg_gen_movi_i64(cpu_dst_64, -1);
                    gen_store_fpr_D(dc, rd, cpu_dst_64);
                    break;
                case 0x07f: /* VIS I fones */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_dst_32 = gen_dest_fpr_F(dc);
                    tcg_gen_movi_i32(cpu_dst_32, -1);
                    gen_store_fpr_F(dc, rd, cpu_dst_32);
                    break;
                case 0x080: /* VIS I shutdown */
                case 0x081: /* VIS II siam */
                    // XXX
                    goto illegal_insn;
                default:
                    goto illegal_insn;
                }
#endif
            } else {
                goto illegal_insn; /* in decodetree */
            }
        }
        break;
    case 3:                     /* load/store instructions */
        {
            unsigned int xop = GET_FIELD(insn, 7, 12);
            /* ??? gen_address_mask prevents us from using a source
               register directly.  Always generate a temporary.  */
            TCGv cpu_addr = tcg_temp_new();

            tcg_gen_mov_tl(cpu_addr, get_src1(dc, insn));
            if (xop == 0x3c || xop == 0x3e) {
                /* V9 casa/casxa : no offset */
            } else if (IS_IMM) {     /* immediate */
                simm = GET_FIELDs(insn, 19, 31);
                if (simm != 0) {
                    tcg_gen_addi_tl(cpu_addr, cpu_addr, simm);
                }
            } else {            /* register */
                rs2 = GET_FIELD(insn, 27, 31);
                if (rs2 != 0) {
                    tcg_gen_add_tl(cpu_addr, cpu_addr, gen_load_gpr(dc, rs2));
                }
            }
            if (xop < 4 || (xop > 7 && xop < 0x14 && xop != 0x0e) ||
                (xop > 0x17 && xop <= 0x1d ) ||
                (xop > 0x2c && xop <= 0x33) || xop == 0x1f || xop == 0x3d) {
                TCGv cpu_val = gen_dest_gpr(dc, rd);

                switch (xop) {
                case 0x0:       /* ld, V9 lduw, load unsigned word */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_ld_tl(cpu_val, cpu_addr,
                                       dc->mem_idx, MO_TEUL | MO_ALIGN);
                    break;
                case 0x1:       /* ldub, load unsigned byte */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_ld_tl(cpu_val, cpu_addr,
                                       dc->mem_idx, MO_UB);
                    break;
                case 0x2:       /* lduh, load unsigned halfword */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_ld_tl(cpu_val, cpu_addr,
                                       dc->mem_idx, MO_TEUW | MO_ALIGN);
                    break;
                case 0x3:       /* ldd, load double word */
                    if (rd & 1)
                        goto illegal_insn;
                    else {
                        TCGv_i64 t64;

                        gen_address_mask(dc, cpu_addr);
                        t64 = tcg_temp_new_i64();
                        tcg_gen_qemu_ld_i64(t64, cpu_addr,
                                            dc->mem_idx, MO_TEUQ | MO_ALIGN);
                        tcg_gen_trunc_i64_tl(cpu_val, t64);
                        tcg_gen_ext32u_tl(cpu_val, cpu_val);
                        gen_store_gpr(dc, rd + 1, cpu_val);
                        tcg_gen_shri_i64(t64, t64, 32);
                        tcg_gen_trunc_i64_tl(cpu_val, t64);
                        tcg_gen_ext32u_tl(cpu_val, cpu_val);
                    }
                    break;
                case 0x9:       /* ldsb, load signed byte */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_ld_tl(cpu_val, cpu_addr, dc->mem_idx, MO_SB);
                    break;
                case 0xa:       /* ldsh, load signed halfword */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_ld_tl(cpu_val, cpu_addr,
                                       dc->mem_idx, MO_TESW | MO_ALIGN);
                    break;
                case 0xd:       /* ldstub */
                    gen_ldstub(dc, cpu_val, cpu_addr, dc->mem_idx);
                    break;
                case 0x0f:
                    /* swap, swap register with memory. Also atomically */
                    cpu_src1 = gen_load_gpr(dc, rd);
                    gen_swap(dc, cpu_val, cpu_src1, cpu_addr,
                             dc->mem_idx, MO_TEUL);
                    break;
#if !defined(CONFIG_USER_ONLY) || defined(TARGET_SPARC64)
                case 0x10:      /* lda, V9 lduwa, load word alternate */
                    gen_ld_asi(dc, cpu_val, cpu_addr, insn, MO_TEUL);
                    break;
                case 0x11:      /* lduba, load unsigned byte alternate */
                    gen_ld_asi(dc, cpu_val, cpu_addr, insn, MO_UB);
                    break;
                case 0x12:      /* lduha, load unsigned halfword alternate */
                    gen_ld_asi(dc, cpu_val, cpu_addr, insn, MO_TEUW);
                    break;
                case 0x13:      /* ldda, load double word alternate */
                    if (rd & 1) {
                        goto illegal_insn;
                    }
                    gen_ldda_asi(dc, cpu_addr, insn, rd);
                    goto skip_move;
                case 0x19:      /* ldsba, load signed byte alternate */
                    gen_ld_asi(dc, cpu_val, cpu_addr, insn, MO_SB);
                    break;
                case 0x1a:      /* ldsha, load signed halfword alternate */
                    gen_ld_asi(dc, cpu_val, cpu_addr, insn, MO_TESW);
                    break;
                case 0x1d:      /* ldstuba -- XXX: should be atomically */
                    gen_ldstub_asi(dc, cpu_val, cpu_addr, insn);
                    break;
                case 0x1f:      /* swapa, swap reg with alt. memory. Also
                                   atomically */
                    cpu_src1 = gen_load_gpr(dc, rd);
                    gen_swap_asi(dc, cpu_val, cpu_src1, cpu_addr, insn);
                    break;
#endif
#ifdef TARGET_SPARC64
                case 0x08: /* V9 ldsw */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_ld_tl(cpu_val, cpu_addr,
                                       dc->mem_idx, MO_TESL | MO_ALIGN);
                    break;
                case 0x0b: /* V9 ldx */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_ld_tl(cpu_val, cpu_addr,
                                       dc->mem_idx, MO_TEUQ | MO_ALIGN);
                    break;
                case 0x18: /* V9 ldswa */
                    gen_ld_asi(dc, cpu_val, cpu_addr, insn, MO_TESL);
                    break;
                case 0x1b: /* V9 ldxa */
                    gen_ld_asi(dc, cpu_val, cpu_addr, insn, MO_TEUQ);
                    break;
                case 0x2d: /* V9 prefetch, no effect */
                    goto skip_move;
                case 0x30: /* V9 ldfa */
                    if (gen_trap_ifnofpu(dc)) {
                        goto jmp_insn;
                    }
                    gen_ldf_asi(dc, cpu_addr, insn, 4, rd);
                    gen_update_fprs_dirty(dc, rd);
                    goto skip_move;
                case 0x33: /* V9 lddfa */
                    if (gen_trap_ifnofpu(dc)) {
                        goto jmp_insn;
                    }
                    gen_ldf_asi(dc, cpu_addr, insn, 8, DFPREG(rd));
                    gen_update_fprs_dirty(dc, DFPREG(rd));
                    goto skip_move;
                case 0x3d: /* V9 prefetcha, no effect */
                    goto skip_move;
                case 0x32: /* V9 ldqfa */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    if (gen_trap_ifnofpu(dc)) {
                        goto jmp_insn;
                    }
                    gen_ldf_asi(dc, cpu_addr, insn, 16, QFPREG(rd));
                    gen_update_fprs_dirty(dc, QFPREG(rd));
                    goto skip_move;
#endif
                default:
                    goto illegal_insn;
                }
                gen_store_gpr(dc, rd, cpu_val);
#if !defined(CONFIG_USER_ONLY) || defined(TARGET_SPARC64)
            skip_move: ;
#endif
            } else if (xop >= 0x20 && xop < 0x24) {
                if (gen_trap_ifnofpu(dc)) {
                    goto jmp_insn;
                }
                switch (xop) {
                case 0x20:      /* ldf, load fpreg */
                    gen_address_mask(dc, cpu_addr);
                    cpu_dst_32 = gen_dest_fpr_F(dc);
                    tcg_gen_qemu_ld_i32(cpu_dst_32, cpu_addr,
                                        dc->mem_idx, MO_TEUL | MO_ALIGN);
                    gen_store_fpr_F(dc, rd, cpu_dst_32);
                    break;
                case 0x21:      /* ldfsr, V9 ldxfsr */
#ifdef TARGET_SPARC64
                    gen_address_mask(dc, cpu_addr);
                    if (rd == 1) {
                        TCGv_i64 t64 = tcg_temp_new_i64();
                        tcg_gen_qemu_ld_i64(t64, cpu_addr,
                                            dc->mem_idx, MO_TEUQ | MO_ALIGN);
                        gen_helper_ldxfsr(cpu_fsr, tcg_env, cpu_fsr, t64);
                        break;
                    }
#endif
                    cpu_dst_32 = tcg_temp_new_i32();
                    tcg_gen_qemu_ld_i32(cpu_dst_32, cpu_addr,
                                        dc->mem_idx, MO_TEUL | MO_ALIGN);
                    gen_helper_ldfsr(cpu_fsr, tcg_env, cpu_fsr, cpu_dst_32);
                    break;
                case 0x22:      /* ldqf, load quad fpreg */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_address_mask(dc, cpu_addr);
                    cpu_src1_64 = tcg_temp_new_i64();
                    tcg_gen_qemu_ld_i64(cpu_src1_64, cpu_addr, dc->mem_idx,
                                        MO_TEUQ | MO_ALIGN_4);
                    tcg_gen_addi_tl(cpu_addr, cpu_addr, 8);
                    cpu_src2_64 = tcg_temp_new_i64();
                    tcg_gen_qemu_ld_i64(cpu_src2_64, cpu_addr, dc->mem_idx,
                                        MO_TEUQ | MO_ALIGN_4);
                    gen_store_fpr_Q(dc, rd, cpu_src1_64, cpu_src2_64);
                    break;
                case 0x23:      /* lddf, load double fpreg */
                    gen_address_mask(dc, cpu_addr);
                    cpu_dst_64 = gen_dest_fpr_D(dc, rd);
                    tcg_gen_qemu_ld_i64(cpu_dst_64, cpu_addr, dc->mem_idx,
                                        MO_TEUQ | MO_ALIGN_4);
                    gen_store_fpr_D(dc, rd, cpu_dst_64);
                    break;
                default:
                    goto illegal_insn;
                }
            } else if (xop < 8 || (xop >= 0x14 && xop < 0x18) ||
                       xop == 0xe || xop == 0x1e) {
                TCGv cpu_val = gen_load_gpr(dc, rd);

                switch (xop) {
                case 0x4: /* st, store word */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_st_tl(cpu_val, cpu_addr,
                                       dc->mem_idx, MO_TEUL | MO_ALIGN);
                    break;
                case 0x5: /* stb, store byte */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_st_tl(cpu_val, cpu_addr, dc->mem_idx, MO_UB);
                    break;
                case 0x6: /* sth, store halfword */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_st_tl(cpu_val, cpu_addr,
                                       dc->mem_idx, MO_TEUW | MO_ALIGN);
                    break;
                case 0x7: /* std, store double word */
                    if (rd & 1)
                        goto illegal_insn;
                    else {
                        TCGv_i64 t64;
                        TCGv lo;

                        gen_address_mask(dc, cpu_addr);
                        lo = gen_load_gpr(dc, rd + 1);
                        t64 = tcg_temp_new_i64();
                        tcg_gen_concat_tl_i64(t64, lo, cpu_val);
                        tcg_gen_qemu_st_i64(t64, cpu_addr,
                                            dc->mem_idx, MO_TEUQ | MO_ALIGN);
                    }
                    break;
#if !defined(CONFIG_USER_ONLY) || defined(TARGET_SPARC64)
                case 0x14: /* sta, V9 stwa, store word alternate */
                    gen_st_asi(dc, cpu_val, cpu_addr, insn, MO_TEUL);
                    break;
                case 0x15: /* stba, store byte alternate */
                    gen_st_asi(dc, cpu_val, cpu_addr, insn, MO_UB);
                    break;
                case 0x16: /* stha, store halfword alternate */
                    gen_st_asi(dc, cpu_val, cpu_addr, insn, MO_TEUW);
                    break;
                case 0x17: /* stda, store double word alternate */
                    if (rd & 1) {
                        goto illegal_insn;
                    }
                    gen_stda_asi(dc, cpu_val, cpu_addr, insn, rd);
                    break;
#endif
#ifdef TARGET_SPARC64
                case 0x0e: /* V9 stx */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_st_tl(cpu_val, cpu_addr,
                                       dc->mem_idx, MO_TEUQ | MO_ALIGN);
                    break;
                case 0x1e: /* V9 stxa */
                    gen_st_asi(dc, cpu_val, cpu_addr, insn, MO_TEUQ);
                    break;
#endif
                default:
                    goto illegal_insn;
                }
            } else if (xop > 0x23 && xop < 0x28) {
                if (gen_trap_ifnofpu(dc)) {
                    goto jmp_insn;
                }
                switch (xop) {
                case 0x24: /* stf, store fpreg */
                    gen_address_mask(dc, cpu_addr);
                    cpu_src1_32 = gen_load_fpr_F(dc, rd);
                    tcg_gen_qemu_st_i32(cpu_src1_32, cpu_addr,
                                        dc->mem_idx, MO_TEUL | MO_ALIGN);
                    break;
                case 0x25: /* stfsr, V9 stxfsr */
                    {
#ifdef TARGET_SPARC64
                        gen_address_mask(dc, cpu_addr);
                        if (rd == 1) {
                            tcg_gen_qemu_st_tl(cpu_fsr, cpu_addr,
                                               dc->mem_idx, MO_TEUQ | MO_ALIGN);
                            break;
                        }
#endif
                        tcg_gen_qemu_st_tl(cpu_fsr, cpu_addr,
                                           dc->mem_idx, MO_TEUL | MO_ALIGN);
                    }
                    break;
                case 0x26:
#ifdef TARGET_SPARC64
                    /* V9 stqf, store quad fpreg */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_address_mask(dc, cpu_addr);
                    /* ??? While stqf only requires 4-byte alignment, it is
                       legal for the cpu to signal the unaligned exception.
                       The OS trap handler is then required to fix it up.
                       For qemu, this avoids having to probe the second page
                       before performing the first write.  */
                    cpu_src1_64 = gen_load_fpr_Q0(dc, rd);
                    tcg_gen_qemu_st_i64(cpu_src1_64, cpu_addr,
                                        dc->mem_idx, MO_TEUQ | MO_ALIGN_16);
                    tcg_gen_addi_tl(cpu_addr, cpu_addr, 8);
                    cpu_src2_64 = gen_load_fpr_Q1(dc, rd);
                    tcg_gen_qemu_st_i64(cpu_src1_64, cpu_addr,
                                        dc->mem_idx, MO_TEUQ);
                    break;
#else /* !TARGET_SPARC64 */
                    /* stdfq, store floating point queue */
#if defined(CONFIG_USER_ONLY)
                    goto illegal_insn;
#else
                    if (!supervisor(dc))
                        goto priv_insn;
                    if (gen_trap_ifnofpu(dc)) {
                        goto jmp_insn;
                    }
                    goto nfq_insn;
#endif
#endif
                case 0x27: /* stdf, store double fpreg */
                    gen_address_mask(dc, cpu_addr);
                    cpu_src1_64 = gen_load_fpr_D(dc, rd);
                    tcg_gen_qemu_st_i64(cpu_src1_64, cpu_addr, dc->mem_idx,
                                        MO_TEUQ | MO_ALIGN_4);
                    break;
                default:
                    goto illegal_insn;
                }
            } else if (xop > 0x33 && xop < 0x3f) {
                switch (xop) {
#ifdef TARGET_SPARC64
                case 0x34: /* V9 stfa */
                    if (gen_trap_ifnofpu(dc)) {
                        goto jmp_insn;
                    }
                    gen_stf_asi(dc, cpu_addr, insn, 4, rd);
                    break;
                case 0x36: /* V9 stqfa */
                    {
                        CHECK_FPU_FEATURE(dc, FLOAT128);
                        if (gen_trap_ifnofpu(dc)) {
                            goto jmp_insn;
                        }
                        gen_stf_asi(dc, cpu_addr, insn, 16, QFPREG(rd));
                    }
                    break;
                case 0x37: /* V9 stdfa */
                    if (gen_trap_ifnofpu(dc)) {
                        goto jmp_insn;
                    }
                    gen_stf_asi(dc, cpu_addr, insn, 8, DFPREG(rd));
                    break;
                case 0x3e: /* V9 casxa */
                    rs2 = GET_FIELD(insn, 27, 31);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_casx_asi(dc, cpu_addr, cpu_src2, insn, rd);
                    break;
#endif
#if !defined(CONFIG_USER_ONLY) || defined(TARGET_SPARC64)
                case 0x3c: /* V9 or LEON3 casa */
#ifndef TARGET_SPARC64
                    CHECK_IU_FEATURE(dc, CASA);
#endif
                    rs2 = GET_FIELD(insn, 27, 31);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_cas_asi(dc, cpu_addr, cpu_src2, insn, rd);
                    break;
#endif
                default:
                    goto illegal_insn;
                }
            } else {
                goto illegal_insn;
            }
        }
        break;
    }
    advance_pc(dc);
 jmp_insn:
    return;
 illegal_insn:
    gen_exception(dc, TT_ILL_INSN);
    return;
#if !defined(CONFIG_USER_ONLY) && !defined(TARGET_SPARC64)
 priv_insn:
    gen_exception(dc, TT_PRIV_INSN);
    return;
#endif
 nfpu_insn:
    gen_op_fpexception_im(dc, FSR_FTT_UNIMPFPOP);
    return;
#if !defined(CONFIG_USER_ONLY) && !defined(TARGET_SPARC64)
 nfq_insn:
    gen_op_fpexception_im(dc, FSR_FTT_SEQ_ERROR);
    return;
#endif
}

static void sparc_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUSPARCState *env = cpu_env(cs);
    int bound;

    dc->pc = dc->base.pc_first;
    dc->npc = (target_ulong)dc->base.tb->cs_base;
    dc->cc_op = CC_OP_DYNAMIC;
    dc->mem_idx = dc->base.tb->flags & TB_FLAG_MMU_MASK;
    dc->def = &env->def;
    dc->fpu_enabled = tb_fpu_enabled(dc->base.tb->flags);
    dc->address_mask_32bit = tb_am_enabled(dc->base.tb->flags);
#ifndef CONFIG_USER_ONLY
    dc->supervisor = (dc->base.tb->flags & TB_FLAG_SUPER) != 0;
#endif
#ifdef TARGET_SPARC64
    dc->fprs_dirty = 0;
    dc->asi = (dc->base.tb->flags >> TB_FLAG_ASI_SHIFT) & 0xff;
#ifndef CONFIG_USER_ONLY
    dc->hypervisor = (dc->base.tb->flags & TB_FLAG_HYPER) != 0;
#endif
#endif
    /*
     * if we reach a page boundary, we stop generation so that the
     * PC of a TT_TFAULT exception is always in the right page
     */
    bound = -(dc->base.pc_first | TARGET_PAGE_MASK) / 4;
    dc->base.max_insns = MIN(dc->base.max_insns, bound);
}

static void sparc_tr_tb_start(DisasContextBase *db, CPUState *cs)
{
}

static void sparc_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    target_ulong npc = dc->npc;

    if (npc & 3) {
        switch (npc) {
        case JUMP_PC:
            assert(dc->jump_pc[1] == dc->pc + 4);
            npc = dc->jump_pc[0] | JUMP_PC;
            break;
        case DYNAMIC_PC:
        case DYNAMIC_PC_LOOKUP:
            npc = DYNAMIC_PC;
            break;
        default:
            g_assert_not_reached();
        }
    }
    tcg_gen_insn_start(dc->pc, npc);
}

static void sparc_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUSPARCState *env = cpu_env(cs);
    unsigned int insn;

    insn = translator_ldl(env, &dc->base, dc->pc);
    dc->base.pc_next += 4;

    if (!decode(dc, insn)) {
        disas_sparc_legacy(dc, insn);
    }

    if (dc->base.is_jmp == DISAS_NORETURN) {
        return;
    }
    if (dc->pc != dc->base.pc_next) {
        dc->base.is_jmp = DISAS_TOO_MANY;
    }
}

static void sparc_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    DisasDelayException *e, *e_next;
    bool may_lookup;

    switch (dc->base.is_jmp) {
    case DISAS_NEXT:
    case DISAS_TOO_MANY:
        if (((dc->pc | dc->npc) & 3) == 0) {
            /* static PC and NPC: we can use direct chaining */
            gen_goto_tb(dc, 0, dc->pc, dc->npc);
            break;
        }

        may_lookup = true;
        if (dc->pc & 3) {
            switch (dc->pc) {
            case DYNAMIC_PC_LOOKUP:
                break;
            case DYNAMIC_PC:
                may_lookup = false;
                break;
            default:
                g_assert_not_reached();
            }
        } else {
            tcg_gen_movi_tl(cpu_pc, dc->pc);
        }

        if (dc->npc & 3) {
            switch (dc->npc) {
            case JUMP_PC:
                gen_generic_branch(dc);
                break;
            case DYNAMIC_PC:
                may_lookup = false;
                break;
            case DYNAMIC_PC_LOOKUP:
                break;
            default:
                g_assert_not_reached();
            }
        } else {
            tcg_gen_movi_tl(cpu_npc, dc->npc);
        }
        if (may_lookup) {
            tcg_gen_lookup_and_goto_ptr();
        } else {
            tcg_gen_exit_tb(NULL, 0);
        }
        break;

    case DISAS_NORETURN:
       break;

    case DISAS_EXIT:
        /* Exit TB */
        save_state(dc);
        tcg_gen_exit_tb(NULL, 0);
        break;

    default:
        g_assert_not_reached();
    }

    for (e = dc->delay_excp_list; e ; e = e_next) {
        gen_set_label(e->lab);

        tcg_gen_movi_tl(cpu_pc, e->pc);
        if (e->npc % 4 == 0) {
            tcg_gen_movi_tl(cpu_npc, e->npc);
        }
        gen_helper_raise_exception(tcg_env, e->excp);

        e_next = e->next;
        g_free(e);
    }
}

static void sparc_tr_disas_log(const DisasContextBase *dcbase,
                               CPUState *cpu, FILE *logfile)
{
    fprintf(logfile, "IN: %s\n", lookup_symbol(dcbase->pc_first));
    target_disas(logfile, cpu, dcbase->pc_first, dcbase->tb->size);
}

static const TranslatorOps sparc_tr_ops = {
    .init_disas_context = sparc_tr_init_disas_context,
    .tb_start           = sparc_tr_tb_start,
    .insn_start         = sparc_tr_insn_start,
    .translate_insn     = sparc_tr_translate_insn,
    .tb_stop            = sparc_tr_tb_stop,
    .disas_log          = sparc_tr_disas_log,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int *max_insns,
                           target_ulong pc, void *host_pc)
{
    DisasContext dc = {};

    translator_loop(cs, tb, max_insns, pc, host_pc, &sparc_tr_ops, &dc.base);
}

void sparc_tcg_init(void)
{
    static const char gregnames[32][4] = {
        "g0", "g1", "g2", "g3", "g4", "g5", "g6", "g7",
        "o0", "o1", "o2", "o3", "o4", "o5", "o6", "o7",
        "l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7",
        "i0", "i1", "i2", "i3", "i4", "i5", "i6", "i7",
    };
    static const char fregnames[32][4] = {
        "f0", "f2", "f4", "f6", "f8", "f10", "f12", "f14",
        "f16", "f18", "f20", "f22", "f24", "f26", "f28", "f30",
        "f32", "f34", "f36", "f38", "f40", "f42", "f44", "f46",
        "f48", "f50", "f52", "f54", "f56", "f58", "f60", "f62",
    };

    static const struct { TCGv_i32 *ptr; int off; const char *name; } r32[] = {
#ifdef TARGET_SPARC64
        { &cpu_xcc, offsetof(CPUSPARCState, xcc), "xcc" },
        { &cpu_fprs, offsetof(CPUSPARCState, fprs), "fprs" },
#endif
        { &cpu_cc_op, offsetof(CPUSPARCState, cc_op), "cc_op" },
        { &cpu_psr, offsetof(CPUSPARCState, psr), "psr" },
    };

    static const struct { TCGv *ptr; int off; const char *name; } rtl[] = {
#ifdef TARGET_SPARC64
        { &cpu_gsr, offsetof(CPUSPARCState, gsr), "gsr" },
#endif
        { &cpu_cond, offsetof(CPUSPARCState, cond), "cond" },
        { &cpu_cc_src, offsetof(CPUSPARCState, cc_src), "cc_src" },
        { &cpu_cc_src2, offsetof(CPUSPARCState, cc_src2), "cc_src2" },
        { &cpu_cc_dst, offsetof(CPUSPARCState, cc_dst), "cc_dst" },
        { &cpu_fsr, offsetof(CPUSPARCState, fsr), "fsr" },
        { &cpu_pc, offsetof(CPUSPARCState, pc), "pc" },
        { &cpu_npc, offsetof(CPUSPARCState, npc), "npc" },
        { &cpu_y, offsetof(CPUSPARCState, y), "y" },
        { &cpu_tbr, offsetof(CPUSPARCState, tbr), "tbr" },
    };

    unsigned int i;

    cpu_regwptr = tcg_global_mem_new_ptr(tcg_env,
                                         offsetof(CPUSPARCState, regwptr),
                                         "regwptr");

    for (i = 0; i < ARRAY_SIZE(r32); ++i) {
        *r32[i].ptr = tcg_global_mem_new_i32(tcg_env, r32[i].off, r32[i].name);
    }

    for (i = 0; i < ARRAY_SIZE(rtl); ++i) {
        *rtl[i].ptr = tcg_global_mem_new(tcg_env, rtl[i].off, rtl[i].name);
    }

    cpu_regs[0] = NULL;
    for (i = 1; i < 8; ++i) {
        cpu_regs[i] = tcg_global_mem_new(tcg_env,
                                         offsetof(CPUSPARCState, gregs[i]),
                                         gregnames[i]);
    }

    for (i = 8; i < 32; ++i) {
        cpu_regs[i] = tcg_global_mem_new(cpu_regwptr,
                                         (i - 8) * sizeof(target_ulong),
                                         gregnames[i]);
    }

    for (i = 0; i < TARGET_DPREGS; i++) {
        cpu_fpr[i] = tcg_global_mem_new_i64(tcg_env,
                                            offsetof(CPUSPARCState, fpr[i]),
                                            fregnames[i]);
    }
}

void sparc_restore_state_to_opc(CPUState *cs,
                                const TranslationBlock *tb,
                                const uint64_t *data)
{
    SPARCCPU *cpu = SPARC_CPU(cs);
    CPUSPARCState *env = &cpu->env;
    target_ulong pc = data[0];
    target_ulong npc = data[1];

    env->pc = pc;
    if (npc == DYNAMIC_PC) {
        /* dynamic NPC: already stored */
    } else if (npc & JUMP_PC) {
        /* jump PC: use 'cond' and the jump targets of the translation */
        if (env->cond) {
            env->npc = npc & ~3;
        } else {
            env->npc = pc + 4;
        }
    } else {
        env->npc = npc;
    }
}
