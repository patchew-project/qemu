/*
 * RISC-V translation routines for the RISC-V Zawrs Extension.
 *
 * Copyright (c) 2022 Christoph Muellner, christoph.muellner@vrull.io
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

#define REQUIRE_ZAWRS(ctx) do {         \
    if (!ctx->cfg_ptr->ext_zawrs) {     \
        return false;                   \
    }                                   \
} while (0)

static bool trans_wrs(DisasContext *ctx, arg_sfence_vm *a)
{
    REQUIRE_ZAWRS(ctx);

    /*
     * We may continue if one or more of the following conditions are met:
     * a) reservation set is invalid
     * b) rs1 != 0 and X(rs1) < time CSR
     * c) interrupt observed
     *
     * A reservation set can be invalidated by any store to a reserved
     * memory location. However, that's currently not implemented in QEMU.
     * So let's just exit the CPU loop and pretend that an interrupt occured.
     */

    /* Clear the load reservation  (if any).  */
    tcg_gen_movi_tl(load_res, -1);

    gen_set_pc_imm(ctx, ctx->pc_succ_insn);
    tcg_gen_exit_tb(NULL, 0);
    ctx->base.is_jmp = DISAS_NORETURN;

    return true;
}
