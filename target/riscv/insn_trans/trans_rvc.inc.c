/*
 * RISC-V translation routines for the RVC Compressed Instruction Set.
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

static bool trans_c_addi4spn(DisasContext *ctx, arg_c_addi4spn *a,
        uint16_t insn)
{
    if (a->nzuimm == 0) {
        /* Reserved in ISA */
        gen_exception_illegal(ctx);
        return true;
    }
    arg_addi arg = { .rd = a->rd, .rs1 = 2, .imm = a->nzuimm };
    return trans_addi(ctx, &arg, insn);
}

static bool trans_c_fld(DisasContext *ctx, arg_c_fld *a, uint16_t insn)
{
    arg_fld arg = { .rd = a->rd, .rs1 = a->rs1, .imm = a->uimm };
    return trans_fld(ctx, &arg, insn);
}

static bool trans_c_lw(DisasContext *ctx, arg_c_lw *a, uint16_t insn)
{
    arg_lw arg = { .rd = a->rd, .rs1 = a->rs1, .imm = a->uimm };
    return trans_lw(ctx, &arg, insn);
}

static bool trans_c_flw_ld(DisasContext *ctx, arg_c_flw_ld *a, uint16_t insn)
{
#ifdef TARGET_RISCV32
    /* C.FLW ( RV32FC-only ) */
    arg_c_lw tmp;
    extract_cl_w(&tmp, insn);
    arg_flw arg = { .rd = tmp.rd, .rs1 = tmp.rs1, .imm = tmp.uimm };
    return trans_flw(ctx, &arg, insn);
#else
    /* C.LD ( RV64C/RV128C-only ) */
    arg_c_fld tmp;
    extract_cl_d(&tmp, insn);
    arg_ld arg = { .rd = tmp.rd, .rs1 = tmp.rs1, .imm = tmp.uimm };
    return trans_ld(ctx, &arg, insn);
#endif
}

static bool trans_c_fsd(DisasContext *ctx, arg_c_fsd *a, uint16_t insn)
{
    arg_fsd arg = { .rs1 = a->rs1, .rs2 = a->rs2, .imm = a->uimm };
    return trans_fsd(ctx, &arg, insn);
}

static bool trans_c_sw(DisasContext *ctx, arg_c_sw *a, uint16_t insn)
{
    arg_sw arg = { .rs1 = a->rs1, .rs2 = a->rs2, .imm = a->uimm };
    return trans_sw(ctx, &arg, insn);
}

static bool trans_c_fsw_sd(DisasContext *ctx, arg_c_fsw_sd *a, uint16_t insn)
{
#ifdef TARGET_RISCV32
    /* C.FSW ( RV32FC-only ) */
    arg_c_sw tmp;
    extract_cs_w(&tmp, insn);
    arg_fsw arg = { .rs1 = tmp.rs1, .rs2 = tmp.rs2, .imm = tmp.uimm };
    return trans_fsw(ctx, &arg, insn);
#else
    /* C.SD ( RV64C/RV128C-only ) */
    arg_c_fsd tmp;
    extract_cs_d(&tmp, insn);
    arg_sd arg = { .rs1 = tmp.rs1, .rs2 = tmp.rs2, .imm = tmp.uimm };
    return trans_sd(ctx, &arg, insn);
#endif
}
