/*
 * RISC-V translation routines for the RVXI Base Integer Instruction Set.
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

static bool trans_illegal(DisasContext *ctx, arg_empty *a)
{
    gen_exception_illegal(ctx);
    return true;
}

static bool trans_c64_illegal(DisasContext *ctx, arg_empty *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    return trans_illegal(ctx, a);
}

static bool trans_lui(DisasContext *ctx, arg_lui *a)
{
    if (a->rd != 0) {
        tcg_gen_movi_tl(cpu_gpr[a->rd], a->imm);
        if (get_xl_max(ctx) == MXL_RV128) {
            tcg_gen_movi_tl(cpu_gprh[a->rd], -(a->imm < 0));
        }
    }
    return true;
}

static bool trans_auipc(DisasContext *ctx, arg_auipc *a)
{
    if (a->rd != 0) {
        if (get_xl_max(ctx) == MXL_RV128) {
            /* TODO : when pc is 128 bits, use all its bits */
            TCGv pc = tcg_constant_tl(ctx->base.pc_next),
                 imml = tcg_constant_tl(a->imm),
                 immh = tcg_constant_tl(-(a->imm < 0)),
                 zero = tcg_constant_tl(0);
            tcg_gen_add2_tl(cpu_gpr[a->rd], cpu_gprh[a->rd],
                            pc, zero,
                            imml, immh);
            return true;
        }
        tcg_gen_movi_tl(cpu_gpr[a->rd], a->imm + ctx->base.pc_next);
        return true;
    }
    return true;
}

static bool trans_jal(DisasContext *ctx, arg_jal *a)
{
    gen_jal(ctx, a->rd, a->imm);
    return true;
}

static bool trans_jalr(DisasContext *ctx, arg_jalr *a)
{
    TCGLabel *misaligned = NULL;

    tcg_gen_addi_tl(cpu_pc, get_gpr(ctx, a->rs1, EXT_NONE), a->imm);
    tcg_gen_andi_tl(cpu_pc, cpu_pc, (target_ulong)-2);

    if (!has_ext(ctx, RVC)) {
        TCGv t0 = tcg_temp_new();

        misaligned = gen_new_label();
        tcg_gen_andi_tl(t0, cpu_pc, 0x2);
        tcg_gen_brcondi_tl(TCG_COND_NE, t0, 0x0, misaligned);
        tcg_temp_free(t0);
    }

    if (a->rd != 0) {
        tcg_gen_movi_tl(cpu_gpr[a->rd], ctx->pc_succ_insn);
    }
    tcg_gen_lookup_and_goto_ptr();

    if (misaligned) {
        gen_set_label(misaligned);
        gen_exception_inst_addr_mis(ctx);
    }
    ctx->base.is_jmp = DISAS_NORETURN;

    return true;
}

/*
 * Comparison predicates using bitwise logic taken from Hacker's Delight, 2.12
 * We are just interested in the sign bit, so rl is not used but for subtracting
 */
static bool gen_setcond_i128(TCGv rl, TCGv rh,
                             TCGv al, TCGv ah,
                             TCGv bl, TCGv bh,
                             TCGCond cond)
{
    switch (cond) {
    case TCG_COND_EQ:
        tcg_gen_setcond_tl(TCG_COND_EQ, rl, al, bl);
        tcg_gen_setcond_tl(TCG_COND_EQ, rh, ah, bh);
        tcg_gen_and_tl(rl, rl, rh);
        break;

    case TCG_COND_NE:
        tcg_gen_setcond_tl(TCG_COND_NE, rl, al, bl);
        tcg_gen_setcond_tl(TCG_COND_NE, rh, ah, bh);
        tcg_gen_or_tl(rl, rl, rh);
        break;

    case TCG_COND_LT:
    {
        TCGv tmp1 = tcg_temp_new(),
             tmp2 = tcg_temp_new();

        tcg_gen_sub2_tl(rl, rh, al, ah, bl, bh);
        tcg_gen_xor_tl(tmp1, rh, ah);
        tcg_gen_xor_tl(tmp2, ah, bh);
        tcg_gen_and_tl(tmp1, tmp1, tmp2);
        tcg_gen_xor_tl(tmp1, rh, tmp1);
        tcg_gen_shri_tl(rl, tmp1, 63);

        tcg_temp_free(tmp1);
        tcg_temp_free(tmp2);
        break;
    }

    case TCG_COND_GE:
        /* Invert result of TCG_COND_LT */
        gen_setcond_i128(rl, rh, al, ah, bl, bh, TCG_COND_LT);
        tcg_gen_xori_tl(rl, rl, 1);
        break;

    case TCG_COND_LTU:
    {
        TCGv tmp1 = tcg_temp_new(),
             tmp2 = tcg_temp_new();

        tcg_gen_sub2_tl(rl, rh, al, ah, bl, bh);
        tcg_gen_eqv_tl(tmp1, ah, bh);
        tcg_gen_and_tl(tmp1, tmp1, rh);
        tcg_gen_andc_tl(tmp2, bh, ah);
        tcg_gen_or_tl(tmp1, tmp1, tmp2);
        tcg_gen_shri_tl(rl, tmp1, 63);

        tcg_temp_free(tmp1);
        tcg_temp_free(tmp2);
        break;
    }

    case TCG_COND_GEU:
        /* Invert result of TCG_COND_LTU */
        gen_setcond_i128(rl, rh, al, ah, bl, bh, TCG_COND_LTU);
        tcg_gen_xori_tl(rl, rl, 1);
        break;

    default:
        return false;
    }
    tcg_gen_movi_tl(rh, 0);
    return true;
}

static bool gen_branch(DisasContext *ctx, arg_b *a, TCGCond cond)
{
    TCGLabel *l = gen_new_label();
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_SIGN);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_SIGN);

    if (get_xl(ctx) == MXL_RV128) {
        TCGv src1h = get_gprh(ctx, a->rs1),
             src2h = get_gprh(ctx, a->rs2),
             tmpl = tcg_temp_new(),
             tmph = tcg_temp_new();

        /*
         * Do not use gen_setcond_i128 for EQ and NE as these conditions are
         * often met and can be more efficiently implemented.
         */
        if (cond == TCG_COND_EQ || cond == TCG_COND_NE) {
            /*
             * bnez and beqz being used quite often too, lets optimize them,
             * although QEMU's tcg optimizer handles these cases nicely
             */
            if (a->rs2 == 0) {
                tcg_gen_or_tl(tmpl, src1, src1h);
                tcg_gen_brcondi_tl(cond, tmpl, 0, l);
            } else {
                tcg_gen_xor_tl(tmpl, src1, src2);
                tcg_gen_xor_tl(tmph, src1h, src2h);
                tcg_gen_or_tl(tmpl, tmpl, tmph);
                tcg_gen_brcondi_tl(cond, tmpl, 0, l);
            }
        } else {
            gen_setcond_i128(tmpl, tmph, src1, src1h, src2, src2h, cond);
            tcg_gen_brcondi_tl(TCG_COND_NE, tmpl, 0, l);
        }

        tcg_temp_free(tmph);
        tcg_temp_free(tmpl);
    } else {
        tcg_gen_brcond_tl(cond, src1, src2, l);
    }
    gen_goto_tb(ctx, 1, ctx->pc_succ_insn);

    gen_set_label(l); /* branch taken */

    if (!has_ext(ctx, RVC) && ((ctx->base.pc_next + a->imm) & 0x3)) {
        /* misaligned */
        gen_exception_inst_addr_mis(ctx);
    } else {
        gen_goto_tb(ctx, 0, ctx->base.pc_next + a->imm);
    }
    ctx->base.is_jmp = DISAS_NORETURN;

    return true;
}

static bool trans_beq(DisasContext *ctx, arg_beq *a)
{
    return gen_branch(ctx, a, TCG_COND_EQ);
}

static bool trans_bne(DisasContext *ctx, arg_bne *a)
{
    return gen_branch(ctx, a, TCG_COND_NE);
}

static bool trans_blt(DisasContext *ctx, arg_blt *a)
{
    return gen_branch(ctx, a, TCG_COND_LT);
}

static bool trans_bge(DisasContext *ctx, arg_bge *a)
{
    return gen_branch(ctx, a, TCG_COND_GE);
}

static bool trans_bltu(DisasContext *ctx, arg_bltu *a)
{
    return gen_branch(ctx, a, TCG_COND_LTU);
}

static bool trans_bgeu(DisasContext *ctx, arg_bgeu *a)
{
    return gen_branch(ctx, a, TCG_COND_GEU);
}

static void gen_addi2_i128(TCGv retl, TCGv reth,
                           TCGv srcl, TCGv srch, target_long imm)
{
    TCGv imml  = tcg_constant_tl(imm),
         immh  = tcg_constant_tl(-(imm < 0));
    tcg_gen_add2_tl(retl, reth, srcl, srch, imml, immh);
}

static bool gen_load_tl(DisasContext *ctx, arg_lb *a, MemOp memop)
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv addr = get_gpr(ctx, a->rs1, EXT_NONE);

    if (a->imm) {
        TCGv temp = temp_new(ctx);
        tcg_gen_addi_tl(temp, addr, a->imm);
        addr = temp;
    }

    tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, memop);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

/*
 * TODO: we should assert that src1h == 0, as we do not change the
 *       address translation mechanism
 */
static bool gen_load_i128(DisasContext *ctx, arg_lb *a, MemOp memop)
{
    TCGv src1l = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src1h = get_gprh(ctx, a->rs1);
    TCGv destl = dest_gpr(ctx, a->rd);
    TCGv desth = dest_gprh(ctx, a->rd);
    TCGv addrl = tcg_temp_new();
    TCGv addrh = tcg_temp_new();
    TCGv imml = tcg_temp_new();
    TCGv immh = tcg_constant_tl(-(a->imm < 0));

    /* Build a 128-bit address */
    if (a->imm != 0) {
        tcg_gen_movi_tl(imml, a->imm);
        tcg_gen_add2_tl(addrl, addrh, src1l, src1h, imml, immh);
    } else {
        tcg_gen_mov_tl(addrl, src1l);
        tcg_gen_mov_tl(addrh, src1h);
    }

    if (memop != (MemOp)MO_TEO) {
        tcg_gen_qemu_ld_tl(destl, addrl, ctx->mem_idx, memop);
        if (memop & MO_SIGN) {
            tcg_gen_sari_tl(desth, destl, 63);
        } else {
            tcg_gen_movi_tl(desth, 0);
        }
    } else {
        tcg_gen_qemu_ld_tl(memop & MO_BSWAP ? desth : destl, addrl,
                           ctx->mem_idx, MO_TEQ);
        gen_addi2_i128(addrl, addrh, addrl, addrh, 8);
        tcg_gen_qemu_ld_tl(memop & MO_BSWAP ? destl : desth, addrl,
                           ctx->mem_idx, MO_TEQ);
    }

    gen_set_gpr(ctx, a->rd, destl);
    gen_set_gprh(ctx, a->rd, desth);

    tcg_temp_free(addrl);
    tcg_temp_free(addrh);
    tcg_temp_free(imml);
    return true;
}

static bool gen_load(DisasContext *ctx, arg_lb *a, MemOp memop)
{
    if (get_xl(ctx) == MXL_RV128) {
        return gen_load_i128(ctx, a, memop);
    } else {
        return gen_load_tl(ctx, a, memop);
    }
}

static bool trans_lb(DisasContext *ctx, arg_lb *a)
{
    return gen_load(ctx, a, MO_SB);
}

static bool trans_lh(DisasContext *ctx, arg_lh *a)
{
    return gen_load(ctx, a, MO_TESW);
}

static bool trans_lw(DisasContext *ctx, arg_lw *a)
{
    return gen_load(ctx, a, MO_TESL);
}

static bool trans_ld(DisasContext *ctx, arg_ld *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    return gen_load(ctx, a, MO_TESQ);
}

static bool trans_lq(DisasContext *ctx, arg_lq *a)
{
    REQUIRE_128BIT(ctx);
    return gen_load(ctx, a, MO_TEO);
}

static bool trans_lbu(DisasContext *ctx, arg_lbu *a)
{
    return gen_load(ctx, a, MO_UB);
}

static bool trans_lhu(DisasContext *ctx, arg_lhu *a)
{
    return gen_load(ctx, a, MO_TEUW);
}

static bool trans_lwu(DisasContext *ctx, arg_lwu *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    return gen_load(ctx, a, MO_TEUL);
}

static bool trans_ldu(DisasContext *ctx, arg_ldu *a)
{
    REQUIRE_128BIT(ctx);
    return gen_load(ctx, a, MO_TEUQ);
}

static bool gen_store_tl(DisasContext *ctx, arg_sb *a, MemOp memop)
{
    TCGv addr = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv data = get_gpr(ctx, a->rs2, EXT_NONE);

    if (a->imm) {
        TCGv temp = temp_new(ctx);
        tcg_gen_addi_tl(temp, addr, a->imm);
        addr = temp;
    }

    tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, memop);
    return true;
}

/*
 * TODO: we should assert that src1h == 0, as we do not change the
 *       address translation mechanism
 */
static bool gen_store_i128(DisasContext *ctx, arg_sb *a, MemOp memop)
{
    TCGv src1l = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src1h = get_gprh(ctx, a->rs1);
    TCGv src2l = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv src2h = get_gprh(ctx, a->rs2);
    TCGv addrl = tcg_temp_new();
    TCGv addrh = tcg_temp_new();
    TCGv imml = tcg_temp_new();
    TCGv immh = tcg_constant_tl(-(a->imm < 0));

    /* Build a 128-bit address */
    if (a->imm != 0) {
        tcg_gen_movi_tl(imml, a->imm);
        tcg_gen_add2_tl(addrl, addrh, src1l, src1h, imml, immh);
    } else {
        tcg_gen_mov_tl(addrl, src1l);
        tcg_gen_mov_tl(addrh, src1h);
    }

    if (memop != (MemOp)MO_TEO) {
        tcg_gen_qemu_st_tl(src2l, addrl, ctx->mem_idx, memop);
    } else {
        tcg_gen_qemu_st_tl(memop & MO_BSWAP ? src2h : src2l, addrl,
            ctx->mem_idx, MO_TEQ);
        gen_addi2_i128(addrl, addrh, addrl, addrh, 8);
        tcg_gen_qemu_st_tl(memop & MO_BSWAP ? src2l : src2h, addrl,
            ctx->mem_idx, MO_TEQ);
    }

    tcg_temp_free(addrl);
    tcg_temp_free(addrh);
    tcg_temp_free(imml);
    return true;
}

static bool gen_store(DisasContext *ctx, arg_sb *a, MemOp memop)
{
    if (get_xl(ctx) == MXL_RV128) {
        return gen_store_i128(ctx, a, memop);
    } else {
        return gen_store_tl(ctx, a, memop);
    }
}

static bool trans_sb(DisasContext *ctx, arg_sb *a)
{
    return gen_store(ctx, a, MO_SB);
}

static bool trans_sh(DisasContext *ctx, arg_sh *a)
{
    return gen_store(ctx, a, MO_TESW);
}

static bool trans_sw(DisasContext *ctx, arg_sw *a)
{
    return gen_store(ctx, a, MO_TESL);
}

static bool trans_sd(DisasContext *ctx, arg_sd *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    return gen_store(ctx, a, MO_TEQ);
}

static bool trans_sq(DisasContext *ctx, arg_sq *a)
{
    REQUIRE_128BIT(ctx);
    return gen_store(ctx, a, MO_TEO);
}

static bool trans_addd(DisasContext *ctx, arg_addd *a)
{
    REQUIRE_128BIT(ctx);
    ctx->ol = MXL_RV64;
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_add_tl, NULL);
}

static bool trans_addid(DisasContext *ctx, arg_addid *a)
{
    REQUIRE_128BIT(ctx);
    ctx->ol = MXL_RV64;
    return gen_arith_imm_fn(ctx, a, EXT_NONE, tcg_gen_addi_tl, NULL);
}

static bool trans_subd(DisasContext *ctx, arg_subd *a)
{
    REQUIRE_128BIT(ctx);
    ctx->ol = MXL_RV64;
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_sub_tl, NULL);
}

static bool trans_addi(DisasContext *ctx, arg_addi *a)
{
    return gen_arith_imm_fn(ctx, a, EXT_NONE, tcg_gen_addi_tl, gen_addi2_i128);
}

static void gen_slt(TCGv ret, TCGv s1, TCGv s2)
{
    tcg_gen_setcond_tl(TCG_COND_LT, ret, s1, s2);
}

static void gen_slt_i128(TCGv retl, TCGv reth,
                         TCGv s1l, TCGv s1h, TCGv s2l, TCGv s2h)
{
    gen_setcond_i128(retl, reth, s1l, s1h, s2l, s2h, TCG_COND_LT);
}

static void gen_sltu(TCGv ret, TCGv s1, TCGv s2)
{
    tcg_gen_setcond_tl(TCG_COND_LTU, ret, s1, s2);
}

static void gen_sltu_i128(TCGv retl, TCGv reth,
                         TCGv s1l, TCGv s1h, TCGv s2l, TCGv s2h)
{
    gen_setcond_i128(retl, reth, s1l, s1h, s2l, s2h, TCG_COND_LTU);
}

static bool trans_slti(DisasContext *ctx, arg_slti *a)
{
    return gen_arith_imm_tl(ctx, a, EXT_SIGN, gen_slt, gen_slt_i128);
}

static bool trans_sltiu(DisasContext *ctx, arg_sltiu *a)
{
    return gen_arith_imm_tl(ctx, a, EXT_SIGN, gen_sltu, gen_sltu_i128);
}

static bool trans_xori(DisasContext *ctx, arg_xori *a)
{
    return gen_logic_imm_fn(ctx, a, EXT_NONE, tcg_gen_xori_tl);
}

static bool trans_ori(DisasContext *ctx, arg_ori *a)
{
    return gen_logic_imm_fn(ctx, a, EXT_NONE, tcg_gen_ori_tl);
}

static bool trans_andi(DisasContext *ctx, arg_andi *a)
{
    return gen_logic_imm_fn(ctx, a, EXT_NONE, tcg_gen_andi_tl);
}

static void gen_slli_i128(TCGv retl, TCGv reth,
                          TCGv src1l, TCGv src1h,
                          target_long shamt)
{
    if (shamt >= 64) {
        tcg_gen_shli_tl(reth, src1l, shamt - 64);
        tcg_gen_movi_tl(retl, 0);
    } else {
        tcg_gen_extract2_tl(reth, src1l, src1h, 64 - shamt);
        tcg_gen_shli_tl(retl, src1l, shamt);
    }
}

static bool trans_slli(DisasContext *ctx, arg_slli *a)
{
    return gen_shift_imm_fn(ctx, a, EXT_NONE, tcg_gen_shli_tl, gen_slli_i128);
}

static void gen_srliw(TCGv dst, TCGv src, target_long shamt)
{
    tcg_gen_extract_tl(dst, src, shamt, 32 - shamt);
}

static void gen_srli_i128(TCGv retl, TCGv reth,
                          TCGv src1l, TCGv src1h,
                          target_long shamt)
{
    if (shamt >= 64) {
        tcg_gen_shri_tl(retl, src1h, shamt - 64);
        tcg_gen_movi_tl(reth, 0);
    } else {
        tcg_gen_extract2_tl(retl, src1l, src1h, shamt);
        tcg_gen_shri_tl(reth, src1h, shamt);
    }
}

static bool trans_srli(DisasContext *ctx, arg_srli *a)
{
    return gen_shift_imm_fn_per_ol(ctx, a, EXT_NONE,
                                   tcg_gen_shri_tl, gen_srliw, gen_srli_i128);
}

static void gen_sraiw(TCGv dst, TCGv src, target_long shamt)
{
    tcg_gen_sextract_tl(dst, src, shamt, 32 - shamt);
}

static void gen_srai_i128(TCGv retl, TCGv reth,
                          TCGv src1l, TCGv src1h,
                          target_long shamt)
{
    if (shamt >= 64) {
        tcg_gen_sari_tl(retl, src1h, shamt - 64);
        tcg_gen_sari_tl(reth, src1h, 63);
    } else {
        tcg_gen_extract2_tl(retl, src1l, src1h, shamt);
        tcg_gen_sari_tl(reth, src1h, shamt);
    }
}

static bool trans_srai(DisasContext *ctx, arg_srai *a)
{
    return gen_shift_imm_fn_per_ol(ctx, a, EXT_NONE,
                                   tcg_gen_sari_tl, gen_sraiw, gen_srai_i128);
}

static bool trans_add(DisasContext *ctx, arg_add *a)
{
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_add_tl, tcg_gen_add2_tl);
}

static bool trans_sub(DisasContext *ctx, arg_sub *a)
{
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_sub_tl, tcg_gen_sub2_tl);
}

enum M128_DIR {
    M128_LEFT,
    M128_RIGHT,
    M128_RIGHT_ARITH
};
/* 127 <= arg2 <= 0 */
static void gen_shift_mod128(TCGv ret, TCGv arg1, TCGv arg2, enum M128_DIR dir)
{
    TCGv tmp1 = tcg_temp_new(),
         tmp2 = tcg_temp_new(),
         sgn = tcg_temp_new(),
         cnst_zero = tcg_constant_tl(0);

    tcg_gen_setcondi_tl(TCG_COND_GEU, tmp1, arg2, 64);

    tcg_gen_andi_tl(tmp2, arg2, 0x3f);
    switch (dir) {
    case M128_LEFT:
        tcg_gen_shl_tl(tmp2, arg1, tmp2);
        break;
    case M128_RIGHT:
        tcg_gen_shr_tl(tmp2, arg1, tmp2);
        break;
    case M128_RIGHT_ARITH:
        tcg_gen_sar_tl(tmp2, arg1, tmp2);
        break;
    }

    if (dir == M128_RIGHT_ARITH) {
        tcg_gen_sari_tl(sgn, arg1, 63);
        tcg_gen_movcond_tl(TCG_COND_NE, ret, tmp1, cnst_zero, sgn, tmp2);
    } else {
        tcg_gen_movcond_tl(TCG_COND_NE, ret, tmp1, cnst_zero, cnst_zero, tmp2);
    }

    tcg_temp_free(tmp1);
    tcg_temp_free(tmp2);
    tcg_temp_free(sgn);
    return;
}

static void gen_sll_i128(TCGv destl, TCGv desth,
                     TCGv src1l, TCGv src1h, TCGv shamt)
{
        TCGv tmp = tcg_temp_new();
        /*
         * From Hacker's Delight 2.17:
         *  y1 = x1 << n | x0 u>> (64 - n) | x0 << (n - 64)
         */
        gen_shift_mod128(desth, src1h, shamt, M128_LEFT);

        tcg_gen_movi_tl(tmp, 64);
        tcg_gen_sub_tl(tmp, tmp, shamt);
        gen_shift_mod128(tmp, src1l, tmp, M128_RIGHT);
        tcg_gen_or_tl(desth, desth, tmp);

        tcg_gen_subi_tl(tmp, shamt, 64);
        gen_shift_mod128(tmp, src1l, tmp, M128_LEFT);
        tcg_gen_or_tl(desth, desth, tmp);

        /* From Hacker's Delight 2.17: y0 = x0 << n */
        gen_shift_mod128(destl, src1l, shamt, M128_LEFT);

        tcg_temp_free(tmp);
}

static bool trans_sll(DisasContext *ctx, arg_sll *a)
{
    return gen_shift(ctx, a, EXT_NONE, tcg_gen_shl_tl, gen_sll_i128);
}

static bool trans_slt(DisasContext *ctx, arg_slt *a)
{
    return gen_arith(ctx, a, EXT_SIGN, gen_slt, gen_slt_i128);
}

static bool trans_sltu(DisasContext *ctx, arg_sltu *a)
{
    return gen_arith(ctx, a, EXT_SIGN, gen_sltu, gen_sltu_i128);
}

static void gen_srl_i128(TCGv destl, TCGv desth,
                         TCGv src1l, TCGv src1h, TCGv shamt)
{
    TCGv tmp = tcg_temp_new();
    /*
     * From Hacker's Delight 2.17:
     * y0 = x0 u>> n | x1 << (64 - n) | x1 u>> (n - 64)
     */
    gen_shift_mod128(destl, src1l, shamt, M128_RIGHT);

    tcg_gen_movi_tl(tmp, 64);
    tcg_gen_sub_tl(tmp, tmp, shamt);
    gen_shift_mod128(tmp, src1h, tmp, M128_LEFT);
    tcg_gen_or_tl(destl, destl, tmp);

    tcg_gen_subi_tl(tmp, shamt, 64);
    gen_shift_mod128(tmp, src1h, tmp, M128_RIGHT);
    tcg_gen_or_tl(destl, destl, tmp);

    /* From Hacker's Delight 2.17 : y1 = x1 u>> n */
    gen_shift_mod128(desth, src1h, shamt, M128_RIGHT);

    tcg_temp_free(tmp);
}

static bool trans_srl(DisasContext *ctx, arg_srl *a)
{
    return gen_shift(ctx, a, EXT_ZERO, tcg_gen_shr_tl, gen_srl_i128);
}

static void gen_sra_i128(TCGv destl, TCGv desth,
                         TCGv src1l, TCGv src1h, TCGv shamt)
{
    TCGv tmp1 = tcg_temp_new(),
         tmp2 = tcg_temp_new(),
         const64 = tcg_constant_tl(64);

    /* Compute y0 value if n < 64: x0 u>> n | x1 << (64 - n) */
    gen_shift_mod128(tmp1, src1l, shamt, M128_RIGHT);
    tcg_gen_movi_tl(tmp2, 64);
    tcg_gen_sub_tl(tmp2, tmp2, shamt);
    gen_shift_mod128(tmp2, src1h, tmp2, M128_LEFT);
    tcg_gen_or_tl(tmp1, tmp1, tmp2);

    /* Compute y0 value if n >= 64: x1 s>> (n - 64) */
    tcg_gen_subi_tl(tmp2, shamt, 64);
    gen_shift_mod128(tmp2, src1h, tmp2, M128_RIGHT_ARITH);

    /* Conditionally move one value or the other */
    tcg_gen_movcond_tl(TCG_COND_LT, destl, shamt, const64, tmp1, tmp2);

    /* y1 = x1 s>> n */
    gen_shift_mod128(desth, src1h, shamt, M128_RIGHT_ARITH);

    tcg_temp_free(tmp1);
    tcg_temp_free(tmp2);
}

static bool trans_sra(DisasContext *ctx, arg_sra *a)
{
    return gen_shift(ctx, a, EXT_SIGN, tcg_gen_sar_tl, gen_sra_i128);
}

static bool trans_xor(DisasContext *ctx, arg_xor *a)
{
    return gen_logic(ctx, a, EXT_NONE, tcg_gen_xor_tl);
}

static bool trans_or(DisasContext *ctx, arg_or *a)
{
    return gen_logic(ctx, a, EXT_NONE, tcg_gen_or_tl);
}

static bool trans_and(DisasContext *ctx, arg_and *a)
{
    return gen_logic(ctx, a, EXT_NONE, tcg_gen_and_tl);
}

static bool trans_addiw(DisasContext *ctx, arg_addiw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_arith_imm_fn(ctx, a, EXT_NONE, tcg_gen_addi_tl, NULL);
}

static bool trans_slliw(DisasContext *ctx, arg_slliw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_shift_imm_fn(ctx, a, EXT_NONE, tcg_gen_shli_tl, NULL);
}

static bool trans_srliw(DisasContext *ctx, arg_srliw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_shift_imm_fn(ctx, a, EXT_NONE, gen_srliw, NULL);
}

static bool trans_sraiw(DisasContext *ctx, arg_sraiw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_shift_imm_fn(ctx, a, EXT_NONE, gen_sraiw, NULL);
}

static bool trans_sllid(DisasContext *ctx, arg_sllid *a)
{
    REQUIRE_128BIT(ctx);
    ctx->ol = MXL_RV64;
    return gen_shift_imm_fn(ctx, a, EXT_NONE, tcg_gen_shli_tl, NULL);
}

static bool trans_srlid(DisasContext *ctx, arg_srlid *a)
{
    REQUIRE_128BIT(ctx);
    ctx->ol = MXL_RV64;
    return gen_shift_imm_fn(ctx, a, EXT_NONE, tcg_gen_shri_tl, NULL);
}

static bool trans_sraid(DisasContext *ctx, arg_sraid *a)
{
    REQUIRE_128BIT(ctx);
    ctx->ol = MXL_RV64;
    return gen_shift_imm_fn(ctx, a, EXT_NONE, tcg_gen_sari_tl,  NULL);
}


static bool trans_addw(DisasContext *ctx, arg_addw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_add_tl, NULL);
}

static bool trans_subw(DisasContext *ctx, arg_subw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_sub_tl, NULL);
}

static bool trans_sllw(DisasContext *ctx, arg_sllw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_shift(ctx, a, EXT_NONE, tcg_gen_shl_tl, NULL);
}

static bool trans_srlw(DisasContext *ctx, arg_srlw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_shift(ctx, a, EXT_ZERO, tcg_gen_shr_tl, NULL);
}

static bool trans_sraw(DisasContext *ctx, arg_sraw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_shift(ctx, a, EXT_SIGN, tcg_gen_sar_tl, NULL);
}

static bool trans_slld(DisasContext *ctx, arg_slld *a)
{
    REQUIRE_128BIT(ctx);
    ctx->ol = MXL_RV64;
    return gen_shift(ctx, a, EXT_NONE, tcg_gen_shl_tl, NULL);
}

static bool trans_srld(DisasContext *ctx, arg_srld *a)
{
    REQUIRE_128BIT(ctx);
    ctx->ol = MXL_RV64;
    return gen_shift(ctx, a, EXT_ZERO, tcg_gen_shr_tl, NULL);
}

static bool trans_srad(DisasContext *ctx, arg_srad *a)
{
    REQUIRE_128BIT(ctx);
    ctx->ol = MXL_RV64;
    return gen_shift(ctx, a, EXT_SIGN, tcg_gen_sar_tl, NULL);
}


static bool trans_fence(DisasContext *ctx, arg_fence *a)
{
    /* FENCE is a full memory barrier. */
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
    return true;
}

static bool trans_fence_i(DisasContext *ctx, arg_fence_i *a)
{
    if (!ctx->ext_ifencei) {
        return false;
    }

    /*
     * FENCE_I is a no-op in QEMU,
     * however we need to end the translation block
     */
    tcg_gen_movi_tl(cpu_pc, ctx->pc_succ_insn);
    tcg_gen_exit_tb(NULL, 0);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool do_csr_post(DisasContext *ctx)
{
    /* We may have changed important cpu state -- exit to main loop. */
    tcg_gen_movi_tl(cpu_pc, ctx->pc_succ_insn);
    tcg_gen_exit_tb(NULL, 0);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool do_csrr(DisasContext *ctx, int rd, int rc)
{
    TCGv dest = dest_gpr(ctx, rd);
    TCGv_i32 csr = tcg_constant_i32(rc);

    if (tb_cflags(ctx->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }
    gen_helper_csrr(dest, cpu_env, csr);
    gen_set_gpr(ctx, rd, dest);
    return do_csr_post(ctx);
}

static bool do_csrw(DisasContext *ctx, int rc, TCGv src)
{
    TCGv_i32 csr = tcg_constant_i32(rc);

    if (tb_cflags(ctx->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }
    gen_helper_csrw(cpu_env, csr, src);
    return do_csr_post(ctx);
}

static bool do_csrrw(DisasContext *ctx, int rd, int rc, TCGv src, TCGv mask)
{
    TCGv dest = dest_gpr(ctx, rd);
    TCGv_i32 csr = tcg_constant_i32(rc);

    if (tb_cflags(ctx->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }
    gen_helper_csrrw(dest, cpu_env, csr, src, mask);
    gen_set_gpr(ctx, rd, dest);
    return do_csr_post(ctx);
}

static bool do_csrr_i128(DisasContext *ctx, int rd, int rc)
{
    TCGv_i32 csr = tcg_constant_i32(rc);

    if (tb_cflags(ctx->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }
    gen_helper_csrr_i128(cpu_env, csr);
    gen_set_gpr(ctx, rd, cpu_hlpr[0]);
    gen_set_gprh(ctx, rd, cpu_hlpr[1]);
    return do_csr_post(ctx);
}

static bool do_csrw_i128(DisasContext *ctx, int rc, TCGv srcl, TCGv srch)
{
    TCGv_i32 csr = tcg_constant_i32(rc);

    if (tb_cflags(ctx->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }
    gen_helper_csrw_i128(cpu_env, csr, srcl, srch);
    return do_csr_post(ctx);
}

static bool do_csrrw_i128(DisasContext *ctx, int rd, int rc,
                          TCGv srcl, TCGv srch, TCGv maskl, TCGv maskh)
{
    TCGv_i32 csr = tcg_constant_i32(rc);

    if (tb_cflags(ctx->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }
    gen_helper_csrrw_i128(cpu_env, csr, srcl, srch, maskl, maskh);
    gen_set_gpr(ctx, rd, cpu_hlpr[0]);
    gen_set_gprh(ctx, rd, cpu_hlpr[1]);
    return do_csr_post(ctx);
}

static bool trans_csrrw(DisasContext *ctx, arg_csrrw *a)
{
    if (get_xl(ctx) < MXL_RV128) {
        TCGv src = get_gpr(ctx, a->rs1, EXT_NONE);

        /*
         * If rd == 0, the insn shall not read the csr, nor cause any of the
         * side effects that might occur on a csr read.
         */
        if (a->rd == 0) {
            return do_csrw(ctx, a->csr, src);
        }

        TCGv mask = tcg_constant_tl(-1);
        return do_csrrw(ctx, a->rd, a->csr, src, mask);
    } else {
        TCGv srcl = get_gpr(ctx, a->rs1, EXT_NONE);
        TCGv srch = get_gprh(ctx, a->rs1);

        /*
         * If rd == 0, the insn shall not read the csr, nor cause any of the
         * side effects that might occur on a csr read.
         */
        if (a->rd == 0) {
            return do_csrw_i128(ctx, a->csr, srcl, srch);
        }

        TCGv mask = tcg_constant_tl(-1);
        return do_csrrw_i128(ctx, a->rd, a->csr, srcl, srch, mask, mask);
    }
}

static bool trans_csrrs(DisasContext *ctx, arg_csrrs *a)
{
    /*
     * If rs1 == 0, the insn shall not write to the csr at all, nor
     * cause any of the side effects that might occur on a csr write.
     * Note that if rs1 specifies a register other than x0, holding
     * a zero value, the instruction will still attempt to write the
     * unmodified value back to the csr and will cause side effects.
     */
    if (get_xl(ctx) < MXL_RV128) {
        if (a->rs1 == 0) {
            return do_csrr(ctx, a->rd, a->csr);
        }

        TCGv ones = tcg_constant_tl(-1);
        TCGv mask = get_gpr(ctx, a->rs1, EXT_ZERO);
        return do_csrrw(ctx, a->rd, a->csr, ones, mask);
    } else {
        if (a->rs1 == 0) {
            return do_csrr_i128(ctx, a->rd, a->csr);
        }

        TCGv ones = tcg_constant_tl(-1);
        TCGv maskl = get_gpr(ctx, a->rs1, EXT_ZERO);
        TCGv maskh = get_gprh(ctx, a->rs1);
        return do_csrrw_i128(ctx, a->rd, a->csr, ones, ones, maskl, maskh);
    }
}

static bool trans_csrrc(DisasContext *ctx, arg_csrrc *a)
{
    /*
     * If rs1 == 0, the insn shall not write to the csr at all, nor
     * cause any of the side effects that might occur on a csr write.
     * Note that if rs1 specifies a register other than x0, holding
     * a zero value, the instruction will still attempt to write the
     * unmodified value back to the csr and will cause side effects.
     */
    if (get_xl(ctx) < MXL_RV128) {
        if (a->rs1 == 0) {
            return do_csrr(ctx, a->rd, a->csr);
        }

        TCGv mask = get_gpr(ctx, a->rs1, EXT_ZERO);
        return do_csrrw(ctx, a->rd, a->csr, ctx->zero, mask);
    } else {
        if (a->rs1 == 0) {
            return do_csrr_i128(ctx, a->rd, a->csr);
        }

        TCGv maskl = get_gpr(ctx, a->rs1, EXT_ZERO);
        TCGv maskh = get_gprh(ctx, a->rs1);
        return do_csrrw_i128(ctx, a->rd, a->csr,
                             ctx->zero, ctx->zero, maskl, maskh);
    }
}

static bool trans_csrrwi(DisasContext *ctx, arg_csrrwi *a)
{
    if (get_xl(ctx) < MXL_RV128) {
        TCGv src = tcg_constant_tl(a->rs1);

        /*
         * If rd == 0, the insn shall not read the csr, nor cause any of the
         * side effects that might occur on a csr read.
         */
        if (a->rd == 0) {
            return do_csrw(ctx, a->csr, src);
        }

        TCGv mask = tcg_constant_tl(-1);
        return do_csrrw(ctx, a->rd, a->csr, src, mask);
    } else {
        TCGv src = tcg_constant_tl(a->rs1);

        /*
         * If rd == 0, the insn shall not read the csr, nor cause any of the
         * side effects that might occur on a csr read.
         */
        if (a->rd == 0) {
            return do_csrw_i128(ctx, a->csr, src, ctx->zero);
        }

        TCGv mask = tcg_constant_tl(-1);
        return do_csrrw_i128(ctx, a->rd, a->csr, src, ctx->zero, mask, mask);
    }
}

static bool trans_csrrsi(DisasContext *ctx, arg_csrrsi *a)
{
    /*
     * If rs1 == 0, the insn shall not write to the csr at all, nor
     * cause any of the side effects that might occur on a csr write.
     * Note that if rs1 specifies a register other than x0, holding
     * a zero value, the instruction will still attempt to write the
     * unmodified value back to the csr and will cause side effects.
     */
    if (get_xl(ctx) < MXL_RV128) {
        if (a->rs1 == 0) {
            return do_csrr(ctx, a->rd, a->csr);
        }

        TCGv ones = tcg_constant_tl(-1);
        TCGv mask = tcg_constant_tl(a->rs1);
        return do_csrrw(ctx, a->rd, a->csr, ones, mask);
    } else {
        if (a->rs1 == 0) {
            return do_csrr_i128(ctx, a->rd, a->csr);
        }

        TCGv ones = tcg_constant_tl(-1);
        TCGv mask = tcg_constant_tl(a->rs1);
        return do_csrrw_i128(ctx, a->rd, a->csr, ones, ones, mask, ctx->zero);
    }
}

static bool trans_csrrci(DisasContext *ctx, arg_csrrci * a)
{
    /*
     * If rs1 == 0, the insn shall not write to the csr at all, nor
     * cause any of the side effects that might occur on a csr write.
     * Note that if rs1 specifies a register other than x0, holding
     * a zero value, the instruction will still attempt to write the
     * unmodified value back to the csr and will cause side effects.
     */
    if (get_xl(ctx) < MXL_RV128) {
        if (a->rs1 == 0) {
            return do_csrr(ctx, a->rd, a->csr);
        }

        TCGv mask = tcg_constant_tl(a->rs1);
        return do_csrrw(ctx, a->rd, a->csr, ctx->zero, mask);
    } else {
        if (a->rs1 == 0) {
            return do_csrr_i128(ctx, a->rd, a->csr);
        }

        TCGv mask = tcg_constant_tl(a->rs1);
        return do_csrrw_i128(ctx, a->rd, a->csr,
                             ctx->zero, ctx->zero, mask, ctx->zero);
    }
}
