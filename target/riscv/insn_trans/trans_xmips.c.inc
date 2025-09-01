/*
 * RISC-V translation routines for the MIPS extensions (xmips*).
 *
 * Copyright (c) 2025 MIPS
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2.1 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Reference: MIPS P8700 instructions
 *            (https://mips.com/products/hardware/p8700/)
 */

#define REQUIRE_XMIPSCMOV(ctx) do {              \
    if (!ctx->cfg_ptr->ext_xmipscmov) {          \
        return false;                            \
    }                                            \
} while (0)

static bool trans_ccmov(DisasContext *ctx, arg_ccmov *a)
{
    REQUIRE_XMIPSCMOV(ctx);

    TCGv zero, source1, source2, source3;
    zero = tcg_constant_tl(0);
    source1 = get_gpr(ctx, a->rs1, EXT_NONE);
    source2 = get_gpr(ctx, a->rs2, EXT_NONE);
    source3 = get_gpr(ctx, a->rs3, EXT_NONE);

    tcg_gen_movcond_tl(TCG_COND_NE, cpu_gpr[a->rd],
                       source2, zero, source1, source3);

    return true;
}
