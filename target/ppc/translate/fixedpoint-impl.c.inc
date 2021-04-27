/*
 * Power ISA decode for Fixed-Point Facility instructions
 *
 * Copyright (c) 2021 Luis Pires <luis.pires@eldorado.org.br>
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

/*
 * Incorporate CIA into the constant when R=1.
 * Validate that when R=1, RA=0.
 */
static bool resolve_PLS_D(DisasContext *ctx, arg_PLS_D *a)
{
    if (a->r) {
        a->si += ctx->cia;
        return a->ra == 0;
    }
    return true;
}

static bool do_ldst_D(DisasContext *ctx, arg_D *a, bool update,
                      bool store, MemOp mop)
{
    TCGv ea;

    if (update && (a->ra == 0 || (!store && a->ra == a->rt))) {
        return false;
    }
    gen_set_access_type(ctx, ACCESS_INT);

    ea = tcg_temp_new();
    if (a->ra) {
        tcg_gen_addi_tl(ea, cpu_gpr[a->ra], a->si);
    } else {
        tcg_gen_movi_tl(ea, a->si);
    }
    if (NARROW_MODE(ctx)) {
        tcg_gen_ext32u_tl(ea, ea);
    }
    mop ^= ctx->default_tcg_memop_mask;
    if (store) {
        tcg_gen_qemu_st_tl(cpu_gpr[a->rt], ea, ctx->mem_idx, mop);
    } else {
        tcg_gen_qemu_ld_tl(cpu_gpr[a->rt], ea, ctx->mem_idx, mop);
    }
    if (update) {
        tcg_gen_mov_tl(cpu_gpr[a->ra], ea);
    }
    tcg_temp_free(ea);

    return true;
}

static bool trans_LBZ(DisasContext *ctx, arg_D *a)
{
    return do_ldst_D(ctx, a, false, false, MO_UB);
}

static bool trans_LBZU(DisasContext *ctx, arg_D *a)
{
    return do_ldst_D(ctx, a, true, false, MO_UB);
}

static bool trans_LHZ(DisasContext *ctx, arg_D *a)
{
    return do_ldst_D(ctx, a, false, false, MO_UW);
}

static bool trans_LHZU(DisasContext *ctx, arg_D *a)
{
    return do_ldst_D(ctx, a, true, false, MO_UW);
}

static bool trans_LHA(DisasContext *ctx, arg_D *a)
{
    return do_ldst_D(ctx, a, false, false, MO_SW);
}

static bool trans_LHAU(DisasContext *ctx, arg_D *a)
{
    return do_ldst_D(ctx, a, true, false, MO_SW);
}

static bool trans_LWZ(DisasContext *ctx, arg_D *a)
{
    return do_ldst_D(ctx, a, false, false, MO_UL);
}

static bool trans_LWZU(DisasContext *ctx, arg_D *a)
{
    return do_ldst_D(ctx, a, true, false, MO_UL);
}

static bool trans_LWA(DisasContext *ctx, arg_D *a)
{
    REQUIRE_INSNS_FLAGS(ctx, 64B);
    return do_ldst_D(ctx, a, false, false, MO_SL);
}

static bool trans_LD(DisasContext *ctx, arg_D *a)
{
    REQUIRE_INSNS_FLAGS(ctx, 64B);
    return do_ldst_D(ctx, a, false, false, MO_Q);
}

static bool trans_LDU(DisasContext *ctx, arg_D *a)
{
    REQUIRE_INSNS_FLAGS(ctx, 64B);
    return do_ldst_D(ctx, a, true, false, MO_Q);
}

static bool trans_STB(DisasContext *ctx, arg_D *a)
{
    return do_ldst_D(ctx, a, false, true, MO_UB);
}

static bool trans_STBU(DisasContext *ctx, arg_D *a)
{
    return do_ldst_D(ctx, a, true, true, MO_UB);
}

static bool trans_STH(DisasContext *ctx, arg_D *a)
{
    return do_ldst_D(ctx, a, false, true, MO_UW);
}

static bool trans_STHU(DisasContext *ctx, arg_D *a)
{
    return do_ldst_D(ctx, a, true, true, MO_UW);
}

static bool trans_STW(DisasContext *ctx, arg_D *a)
{
    return do_ldst_D(ctx, a, false, true, MO_UL);
}

static bool trans_STWU(DisasContext *ctx, arg_D *a)
{
    return do_ldst_D(ctx, a, true, true, MO_UL);
}

static bool trans_STD(DisasContext *ctx, arg_D *a)
{
    REQUIRE_INSNS_FLAGS(ctx, 64B);
    return do_ldst_D(ctx, a, false, true, MO_Q);
}

static bool trans_STDU(DisasContext *ctx, arg_D *a)
{
    REQUIRE_INSNS_FLAGS(ctx, 64B);
    return do_ldst_D(ctx, a, true, true, MO_Q);
}

static bool do_ldst_X(DisasContext *ctx, arg_X *a, bool update,
                      bool store, MemOp mop)
{
    TCGv ea;

    if (update && (a->ra == 0 || (!store && a->ra == a->rt))) {
        return false;
    }
    gen_set_access_type(ctx, ACCESS_INT);

    ea = tcg_temp_new();
    if (a->ra) {
        tcg_gen_add_tl(ea, cpu_gpr[a->ra], cpu_gpr[a->rb]);
    } else {
        tcg_gen_mov_tl(ea, cpu_gpr[a->rb]);
    }
    if (NARROW_MODE(ctx)) {
        tcg_gen_ext32u_tl(ea, ea);
    }
    mop ^= ctx->default_tcg_memop_mask;
    if (store) {
        tcg_gen_qemu_st_tl(cpu_gpr[a->rt], ea, ctx->mem_idx, mop);
    } else {
        tcg_gen_qemu_ld_tl(cpu_gpr[a->rt], ea, ctx->mem_idx, mop);
    }
    if (update) {
        tcg_gen_mov_tl(cpu_gpr[a->ra], ea);
    }
    tcg_temp_free(ea);

    return true;
}

static bool trans_LBZX(DisasContext *ctx, arg_X *a)
{
    return do_ldst_X(ctx, a, false, false, MO_UB);
}

static bool trans_LBZUX(DisasContext *ctx, arg_X *a)
{
    return do_ldst_X(ctx, a, true, false, MO_UB);
}

static bool trans_LHZX(DisasContext *ctx, arg_X *a)
{
    return do_ldst_X(ctx, a, false, false, MO_UW);
}

static bool trans_LHZUX(DisasContext *ctx, arg_X *a)
{
    return do_ldst_X(ctx, a, true, false, MO_UW);
}

static bool trans_LHAX(DisasContext *ctx, arg_X *a)
{
    return do_ldst_X(ctx, a, false, false, MO_SW);
}

static bool trans_LHAXU(DisasContext *ctx, arg_X *a)
{
    return do_ldst_X(ctx, a, true, false, MO_SW);
}

static bool trans_LWZX(DisasContext *ctx, arg_X *a)
{
    return do_ldst_X(ctx, a, false, false, MO_UL);
}

static bool trans_LWZUX(DisasContext *ctx, arg_X *a)
{
    return do_ldst_X(ctx, a, true, false, MO_UL);
}

static bool trans_LWAX(DisasContext *ctx, arg_X *a)
{
    REQUIRE_INSNS_FLAGS(ctx, 64B);
    return do_ldst_X(ctx, a, false, false, MO_SL);
}

static bool trans_LWAUX(DisasContext *ctx, arg_X *a)
{
    REQUIRE_INSNS_FLAGS(ctx, 64B);
    return do_ldst_X(ctx, a, true, false, MO_SL);
}

static bool trans_LDX(DisasContext *ctx, arg_X *a)
{
    REQUIRE_INSNS_FLAGS(ctx, 64B);
    return do_ldst_X(ctx, a, false, false, MO_Q);
}

static bool trans_LDUX(DisasContext *ctx, arg_X *a)
{
    REQUIRE_INSNS_FLAGS(ctx, 64B);
    return do_ldst_X(ctx, a, true, false, MO_Q);
}

static bool trans_STBX(DisasContext *ctx, arg_X *a)
{
    return do_ldst_X(ctx, a, false, true, MO_UB);
}

static bool trans_STBUX(DisasContext *ctx, arg_X *a)
{
    return do_ldst_X(ctx, a, true, true, MO_UB);
}

static bool trans_STHX(DisasContext *ctx, arg_X *a)
{
    return do_ldst_X(ctx, a, false, true, MO_UW);
}

static bool trans_STHUX(DisasContext *ctx, arg_X *a)
{
    return do_ldst_X(ctx, a, true, true, MO_UW);
}

static bool trans_STWX(DisasContext *ctx, arg_X *a)
{
    return do_ldst_X(ctx, a, false, true, MO_UL);
}

static bool trans_STWUX(DisasContext *ctx, arg_X *a)
{
    return do_ldst_X(ctx, a, true, true, MO_UL);
}

static bool trans_STDX(DisasContext *ctx, arg_X *a)
{
    REQUIRE_INSNS_FLAGS(ctx, 64B);
    return do_ldst_X(ctx, a, false, true, MO_Q);
}

static bool trans_STDUX(DisasContext *ctx, arg_X *a)
{
    REQUIRE_INSNS_FLAGS(ctx, 64B);
    return do_ldst_X(ctx, a, true, true, MO_Q);
}

static bool do_ldst_PLS_D(DisasContext *ctx, arg_PLS_D *a,
                          bool store, MemOp mop)
{
    TCGv ea;

    if (!resolve_PLS_D(ctx, a)) {
        return false;
    }
    gen_set_access_type(ctx, ACCESS_INT);

    ea = tcg_temp_new();
    if (a->ra) {
        tcg_gen_addi_tl(ea, cpu_gpr[a->ra], a->si);
    } else {
        tcg_gen_movi_tl(ea, a->si);
    }
    if (NARROW_MODE(ctx)) {
        tcg_gen_ext32u_tl(ea, ea);
    }
    mop ^= ctx->default_tcg_memop_mask;
    if (store) {
        tcg_gen_qemu_st_tl(cpu_gpr[a->rt], ea, ctx->mem_idx, mop);
    } else {
        tcg_gen_qemu_ld_tl(cpu_gpr[a->rt], ea, ctx->mem_idx, mop);
    }
    tcg_temp_free(ea);

    return true;
}

static bool trans_PLBZ(DisasContext *ctx, arg_PLS_D *a)
{
    return do_ldst_PLS_D(ctx, a, false, MO_UB);
}

static bool trans_PLHZ(DisasContext *ctx, arg_PLS_D *a)
{
    return do_ldst_PLS_D(ctx, a, false, MO_UW);
}

static bool trans_PLHA(DisasContext *ctx, arg_PLS_D *a)
{
    return do_ldst_PLS_D(ctx, a, false, MO_SW);
}

static bool trans_PLWZ(DisasContext *ctx, arg_PLS_D *a)
{
    return do_ldst_PLS_D(ctx, a, false, MO_UL);
}

static bool trans_PLWA(DisasContext *ctx, arg_PLS_D *a)
{
    return do_ldst_PLS_D(ctx, a, false, MO_SL);
}

static bool trans_PLD(DisasContext *ctx, arg_PLS_D *a)
{
    return do_ldst_PLS_D(ctx, a, false, MO_Q);
}

static bool trans_PSTB(DisasContext *ctx, arg_PLS_D *a)
{
    return do_ldst_PLS_D(ctx, a, true, MO_UB);
}

static bool trans_PSTH(DisasContext *ctx, arg_PLS_D *a)
{
    return do_ldst_PLS_D(ctx, a, true, MO_UW);
}

static bool trans_PSTW(DisasContext *ctx, arg_PLS_D *a)
{
    return do_ldst_PLS_D(ctx, a, true, MO_UL);
}

static bool trans_PSTD(DisasContext *ctx, arg_PLS_D *a)
{
    return do_ldst_PLS_D(ctx, a, true, MO_Q);
}

static bool trans_ADDI(DisasContext *ctx, arg_D *a)
{
    if (a->ra) {
        tcg_gen_addi_tl(cpu_gpr[a->rt], cpu_gpr[a->ra], a->si);
    } else {
        tcg_gen_movi_tl(cpu_gpr[a->rt], a->si);
    }
    return true;
}

static bool trans_ADDIS(DisasContext *ctx, arg_D *a)
{
    a->si <<= 16;
    return trans_ADDI(ctx, a);
}

static bool trans_PADDI(DisasContext *ctx, arg_PLS_D *a)
{
    if (!resolve_PLS_D(ctx, a)) {
        return false;
    }
    if (a->ra) {
        tcg_gen_addi_tl(cpu_gpr[a->rt], cpu_gpr[a->ra], a->si);
    } else {
        tcg_gen_movi_tl(cpu_gpr[a->rt], a->si);
    }
    return true;
}

static bool trans_PNOP(DisasContext *ctx, arg_PNOP *a)
{
    /*
     * TODO: diagnose the set of patterns that are illegal:
     * branches, rfebb, sync other than isync, or a
     * service processor attention.
     * The Engineering Note allows us to either diagnose
     * these as illegal, or treat them all as no-op.
     */
    return true;
}
