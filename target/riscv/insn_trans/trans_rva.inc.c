/*
 * RISC-V translation routines for the RV64A Standard Extension.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2018 Peer Adelt, peer.adelt@hni.uni-paderborn.de
 *                    Bastian Koppelmann, kbastian@mail.uni-paderborn.de
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

static inline bool gen_lr(DisasContext *ctx, arg_atomic *a, TCGMemOp mop)
{
    TCGv src1 = tcg_temp_new();
    /* Put addr in load_res, data in load_val.  */
    gen_get_gpr(src1, a->rs1);
    if (a->rl) {
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_STRL);
    }
    tcg_gen_qemu_ld_tl(load_val, src1, ctx->mem_idx, mop);
    if (a->aq) {
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_LDAQ);
    }
    tcg_gen_mov_tl(load_res, src1);
    gen_set_gpr(a->rd, load_val);

    tcg_temp_free(src1);
    return true;
}

static inline bool gen_sc(DisasContext *ctx, arg_atomic *a, TCGMemOp mop)
{
    TCGv src1 = tcg_temp_new();
    TCGv src2 = tcg_temp_new();
    TCGv dat = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();

    gen_get_gpr(src1, a->rs1);
    tcg_gen_brcond_tl(TCG_COND_NE, load_res, src1, l1);

    gen_get_gpr(src2, a->rs2);
    /* Note that the TCG atomic primitives are SC,
       so we can ignore AQ/RL along this path.  */
    tcg_gen_atomic_cmpxchg_tl(src1, load_res, load_val, src2,
                              ctx->mem_idx, mop);
    tcg_gen_setcond_tl(TCG_COND_NE, dat, src1, load_val);
    gen_set_gpr(a->rd, dat);
    tcg_gen_br(l2);

    gen_set_label(l1);
    /* Address comparion failure.  However, we still need to
       provide the memory barrier implied by AQ/RL.  */
    tcg_gen_mb(TCG_MO_ALL + a->aq * TCG_BAR_LDAQ + a->rl * TCG_BAR_STRL);
    tcg_gen_movi_tl(dat, 1);
    gen_set_gpr(a->rd, dat);

    gen_set_label(l2);
    tcg_temp_free(dat);
    tcg_temp_free(src1);
    tcg_temp_free(src2);
    return true;
}

static bool gen_amo(DisasContext *ctx, arg_atomic *a, uint32_t opc,
                    TCGMemOp mop)
{
    TCGv src1 = tcg_temp_new();
    TCGv src2 = tcg_temp_new();

    gen_get_gpr(src1, a->rs1);
    gen_get_gpr(src2, a->rs2);

    switch (opc) {
    case OPC_RISC_AMOSWAP:
        /* Note that the TCG atomic primitives are SC,
           so we can ignore AQ/RL along this path.  */
        tcg_gen_atomic_xchg_tl(src2, src1, src2, ctx->mem_idx, mop);
        break;
    case OPC_RISC_AMOADD:
        tcg_gen_atomic_fetch_add_tl(src2, src1, src2, ctx->mem_idx, mop);
        break;
    case OPC_RISC_AMOXOR:
        tcg_gen_atomic_fetch_xor_tl(src2, src1, src2, ctx->mem_idx, mop);
        break;
    case OPC_RISC_AMOAND:
        tcg_gen_atomic_fetch_and_tl(src2, src1, src2, ctx->mem_idx, mop);
        break;
    case OPC_RISC_AMOOR:
        tcg_gen_atomic_fetch_or_tl(src2, src1, src2, ctx->mem_idx, mop);
        break;
    case OPC_RISC_AMOMIN:
        tcg_gen_atomic_fetch_smin_tl(src2, src1, src2, ctx->mem_idx, mop);
        break;
    case OPC_RISC_AMOMAX:
        tcg_gen_atomic_fetch_smax_tl(src2, src1, src2, ctx->mem_idx, mop);
        break;
    case OPC_RISC_AMOMINU:
        tcg_gen_atomic_fetch_umin_tl(src2, src1, src2, ctx->mem_idx, mop);
        break;
    case OPC_RISC_AMOMAXU:
        tcg_gen_atomic_fetch_umax_tl(src2, src1, src2, ctx->mem_idx, mop);
        break;
    default:
        return false;
    }
    gen_set_gpr(a->rd, src2);
    tcg_temp_free(src1);
    tcg_temp_free(src2);
    return true;
}

static bool trans_lr_w(DisasContext *ctx, arg_lr_w *a, uint32_t insn)
{
    return gen_lr(ctx, a, (MO_ALIGN | MO_TESL));
}

static bool trans_sc_w(DisasContext *ctx, arg_sc_w *a, uint32_t insn)
{
    return gen_sc(ctx, a, (MO_ALIGN | MO_TESL));
}

static bool trans_amoswap_w(DisasContext *ctx, arg_amoswap_w *a, uint32_t insn)
{
    return gen_amo(ctx, a, OPC_RISC_AMOSWAP, (MO_ALIGN | MO_TESL));
}

static bool trans_amoadd_w(DisasContext *ctx, arg_amoadd_w *a, uint32_t insn)
{
    return gen_amo(ctx, a, OPC_RISC_AMOADD, (MO_ALIGN | MO_TESL));
}

static bool trans_amoxor_w(DisasContext *ctx, arg_amoxor_w *a, uint32_t insn)
{
    return gen_amo(ctx, a, OPC_RISC_AMOXOR, (MO_ALIGN | MO_TESL));
}

static bool trans_amoand_w(DisasContext *ctx, arg_amoand_w *a, uint32_t insn)
{
    return gen_amo(ctx, a, OPC_RISC_AMOAND, (MO_ALIGN | MO_TESL));
}

static bool trans_amoor_w(DisasContext *ctx, arg_amoor_w *a, uint32_t insn)
{
    return gen_amo(ctx, a, OPC_RISC_AMOOR, (MO_ALIGN | MO_TESL));
}

static bool trans_amomin_w(DisasContext *ctx, arg_amomin_w *a, uint32_t insn)
{
    return gen_amo(ctx, a, OPC_RISC_AMOMIN, (MO_ALIGN | MO_TESL));
}

static bool trans_amomax_w(DisasContext *ctx, arg_amomax_w *a, uint32_t insn)
{
    return gen_amo(ctx, a, OPC_RISC_AMOMAX, (MO_ALIGN | MO_TESL));
}

static bool trans_amominu_w(DisasContext *ctx, arg_amominu_w *a, uint32_t insn)
{
    return gen_amo(ctx, a, OPC_RISC_AMOMINU, (MO_ALIGN | MO_TESL));
}

static bool trans_amomaxu_w(DisasContext *ctx, arg_amomaxu_w *a, uint32_t insn)
{
    return gen_amo(ctx, a, OPC_RISC_AMOMAXU, (MO_ALIGN | MO_TESL));
}
