/*
 * RISC-V translation routines for the Zk[nd,ne,nh,sed,sh] Standard Extension.
 *
 * Copyright (c) 2021 Ruibo Lu, luruibo2000@163.com
 * Copyright (c) 2021 Zewen Ye, lustrew@foxmail.com
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

#define REQUIRE_ZKND(ctx) do {                  \
    if (!ctx->cfg_ptr->ext_zknd) {    \
        return false;                           \
    }                                           \
} while (0)

#define REQUIRE_ZKNE(ctx) do {                  \
    if (!ctx->cfg_ptr->ext_zkne) {    \
        return false;                           \
    }                                           \
} while (0)

#define REQUIRE_ZKNH(ctx) do {                  \
    if (!ctx->cfg_ptr->ext_zknh) {    \
        return false;                           \
    }                                           \
} while (0)

static bool trans_aes32esmi(DisasContext *ctx, arg_aes32esmi *a)
{
    REQUIRE_ZKNE(ctx);

    TCGv bs = tcg_const_tl(a->bs);
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_aes32esmi(dest, src1, src2, bs);
    gen_set_gpr(ctx, a->rd, dest);

    tcg_temp_free(bs);
    return true;
}

static bool trans_aes64esm(DisasContext *ctx, arg_aes64esm *a)
{
    REQUIRE_ZKNE(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_aes64esm(dest, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_aes32esi(DisasContext *ctx, arg_aes32esi *a)
{
    REQUIRE_ZKNE(ctx);

    TCGv bs = tcg_const_tl(a->bs);
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_aes32esi(dest, src1, src2, bs);
    gen_set_gpr(ctx, a->rd, dest);

    tcg_temp_free(bs);
    return true;
}

static bool trans_aes64es(DisasContext *ctx, arg_aes64es *a)
{
    REQUIRE_ZKNE(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_aes64es(dest, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_aes32dsmi(DisasContext *ctx, arg_aes32dsmi *a)
{
    REQUIRE_ZKND(ctx);

    TCGv bs = tcg_const_tl(a->bs);
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_aes32dsmi(dest, src1, src2, bs);
    gen_set_gpr(ctx, a->rd, dest);

    tcg_temp_free(bs);
    return true;
}

static bool trans_aes64dsm(DisasContext *ctx, arg_aes64dsm *a)
{
    REQUIRE_ZKND(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_aes64dsm(dest, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_aes64ks2(DisasContext *ctx, arg_aes64ks2 *a)
{
    REQUIRE_EITHER_EXT(ctx, zknd, zkne);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_aes64ks2(dest, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_aes32dsi(DisasContext *ctx, arg_aes32dsi *a)
{
    REQUIRE_ZKND(ctx);

    TCGv bs = tcg_const_tl(a->bs);
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_aes32dsi(dest, src1, src2, bs);
    gen_set_gpr(ctx, a->rd, dest);

    tcg_temp_free(bs);
    return true;
}

static bool trans_aes64ds(DisasContext *ctx, arg_aes64ds *a)
{
    REQUIRE_ZKND(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_aes64ds(dest, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_aes64ks1i(DisasContext *ctx, arg_aes64ks1i *a)
{
    REQUIRE_EITHER_EXT(ctx, zknd, zkne);

    if (a->rnum > 0xA) {
        return false;
    }

    TCGv rnum = tcg_const_tl(a->rnum);
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);

    gen_helper_aes64ks1i(dest, src1, rnum);
    gen_set_gpr(ctx, a->rd, dest);

    tcg_temp_free(rnum);
    return true;
}

static bool trans_aes64im(DisasContext *ctx, arg_aes64im *a)
{
    REQUIRE_ZKND(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);

    gen_helper_aes64im(dest, src1);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_sha256sig0(DisasContext *ctx, arg_sha256sig0 *a)
{
    REQUIRE_ZKNH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);

    gen_helper_sha256sig0(dest, src1);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_sha256sig1(DisasContext *ctx, arg_sha256sig1 *a)
{
    REQUIRE_ZKNH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);

    gen_helper_sha256sig1(dest, src1);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_sha256sum0(DisasContext *ctx, arg_sha256sum0 *a)
{
    REQUIRE_ZKNH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);

    gen_helper_sha256sum0(dest, src1);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_sha256sum1(DisasContext *ctx, arg_sha256sum1 *a)
{
    REQUIRE_ZKNH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);

    gen_helper_sha256sum1(dest, src1);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_sha512sum0r(DisasContext *ctx, arg_sha512sum0r *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZKNH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_sha512sum0r(dest, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_sha512sum1r(DisasContext *ctx, arg_sha512sum1r *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZKNH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_sha512sum1r(dest, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_sha512sig0l(DisasContext *ctx, arg_sha512sig0l *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZKNH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_sha512sig0l(dest, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_sha512sig0h(DisasContext *ctx, arg_sha512sig0h *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZKNH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_sha512sig0h(dest, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_sha512sig1l(DisasContext *ctx, arg_sha512sig1l *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZKNH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_sha512sig1l(dest, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_sha512sig1h(DisasContext *ctx, arg_sha512sig1h *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZKNH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_sha512sig1h(dest, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_sha512sig0(DisasContext *ctx, arg_sha512sig0 *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZKNH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);

    gen_helper_sha512sig0(dest, src1);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_sha512sig1(DisasContext *ctx, arg_sha512sig1 *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZKNH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);

    gen_helper_sha512sig1(dest, src1);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_sha512sum0(DisasContext *ctx, arg_sha512sum0 *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZKNH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);

    gen_helper_sha512sum0(dest, src1);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_sha512sum1(DisasContext *ctx, arg_sha512sum1 *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZKNH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);

    gen_helper_sha512sum1(dest, src1);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}
