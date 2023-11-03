#!/usr/bin/env python3

##
##  Copyright(c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
##
##  This program is free software; you can redistribute it and/or modify
##  it under the terms of the GNU General Public License as published by
##  the Free Software Foundation; either version 2 of the License, or
##  (at your option) any later version.
##
##  This program is distributed in the hope that it will be useful,
##  but WITHOUT ANY WARRANTY; without even the implied warranty of
##  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
##  GNU General Public License for more details.
##
##  You should have received a copy of the GNU General Public License
##  along with this program; if not, see <http://www.gnu.org/licenses/>.
##

import sys
import re
import string
import hex_common

##
## Helpers for gen_analyze_func
##
def is_predicated(tag):
    return "A_CONDEXEC" in hex_common.attribdict[tag]

def vreg_write_type(tag):
    newv = "EXT_DFL"
    if hex_common.is_new_result(tag):
        newv = "EXT_NEW"
    elif hex_common.is_tmp_result(tag):
        newv = "EXT_TMP"
    return newv

def declare_regn(f, tag, regtype, regid, regno):
    regN = f"{regtype}{regid}N"
    if regtype == "C":
        f.write(
            f"    const int {regN} = insn->regno[{regno}] "
            "+ HEX_REG_SA0;\n"
        )
    else:
        f.write(f"    const int {regN} = insn->regno[{regno}];\n")

def analyze_read(f, tag, regtype, regid, regno):
    regN = f"{regtype}{regid}N"
    if hex_common.is_pair(regid):
        if regtype in {"R",  "C"}:
            f.write(f"    ctx_log_reg_read_pair(ctx, {regN});\n")
        elif regtype == "V":
            f.write(
                f"    ctx_log_vreg_read_pair(ctx, {regN}, "
                "insn_has_hvx_helper);\n"
            )
        else:
            hex_common.bad_register(regtype, regid)
    elif hex_common.is_single(regid):
        if hex_common.is_old_val(regtype, regid, tag):
            if regtype in {"R", "C", "M"}:
                f.write(f"    ctx_log_reg_read(ctx, {regN});\n")
            elif regtype == "P":
                f.write(f"    ctx_log_pred_read(ctx, {regN});\n")
            elif regtype in {"V", "O"}:
                f.write(
                    f"    ctx_log_vreg_read(ctx, {regN}, "
                    "insn_has_hvx_helper);\n"
                )
            elif regtype == "Q":
                f.write(
                    f"    ctx_log_qreg_read(ctx, {regN}, "
                    "insn_has_hvx_helper);\n"
                )
            else:
                hex_common.bad_register(regtype, regid)
        elif hex_common.is_new_val(regtype, regid, tag):
            if regtype == "N":
                f.write(f"    ctx_log_reg_read_new(ctx, {regN});\n")
            elif regtype == "P":
                f.write(f"    ctx_log_pred_read_new(ctx, {regN});\n")
            elif regtype == "O":
                f.write(
                    f"    ctx_log_vreg_read_new(ctx, {regN}, "
                    "insn_has_hvx_helper);\n"
                )
            else:
                hex_common.bad_register(regtype, regid)
        else:
            hex_common.bad_register(regtype, regid)
    else:
        hex_common.bad_register(regtype, regid)

def analyze_write(f, tag, regtype, regid, regno):
    regN = f"{regtype}{regid}N"
    predicated = "true" if is_predicated(tag) else "false"
    if hex_common.is_pair(regid):
        if regtype in {"R", "C"}:
            f.write(f"    ctx_log_reg_write_pair(ctx, {regN}, {predicated});\n")
        elif regtype == "V":
            f.write(
                f"    ctx_log_vreg_write_pair(ctx, {regN}, "
                f"{vreg_write_type(tag)}, {predicated}, "
                "insn_has_hvx_helper);\n"
            )
        else:
            hex_common.bad_register(regtype, regid)
    elif hex_common.is_single(regid):
        if regtype in {"R", "C"}:
            f.write(f"    ctx_log_reg_write(ctx, {regN}, {predicated});\n")
        elif regtype == "P":
            f.write(f"    ctx_log_pred_write(ctx, {regN});\n")
        elif regtype == "V":
            f.write(
                f"    ctx_log_vreg_write(ctx, {regN}, "
                f"{vreg_write_type(tag)}, {predicated}, "
                "insn_has_hvx_helper);\n"
            )
        elif regtype == "Q":
            f.write(
                f"    ctx_log_qreg_write(ctx, {regN}, "
                "insn_has_hvx_helper);\n"
            )
        else:
            hex_common.bad_register(regtype, regid)
    else:
        hex_common.bad_register(regtype, regid)


##
## Generate the code to analyze the instruction
##     For A2_add: Rd32=add(Rs32,Rt32), { RdV=RsV+RtV;}
##     We produce:
##     static void analyze_A2_add(DisasContext *ctx)
##     {
##         Insn *insn G_GNUC_UNUSED = ctx->insn;
##         const int RdN = insn->regno[0];
##         const int RsN = insn->regno[1];
##         const int RtN = insn->regno[2];
##         ctx_log_reg_read(ctx, RsN);
##         ctx_log_reg_read(ctx, RtN);
##         ctx_log_reg_write(ctx, RdN, false);
##     }
##
def gen_analyze_func(f, tag, regs, imms):
    f.write(f"static void analyze_{tag}(DisasContext *ctx)\n")
    f.write("{\n")

    f.write("    Insn *insn G_GNUC_UNUSED = ctx->insn;\n")
    if (hex_common.is_hvx_insn(tag)):
        if hex_common.has_hvx_helper(tag):
            f.write(
                "    const bool G_GNUC_UNUSED insn_has_hvx_helper = true;\n"
            )
            f.write("    ctx_start_hvx_insn(ctx);\n")
        else:
            f.write(
                "    const bool G_GNUC_UNUSED insn_has_hvx_helper = false;\n"
            )


    ## Declare the operands
    i = 0
    for regtype, regid in regs:
        declare_regn(f, tag, regtype, regid, i)
        i += 1

    ## Analyze the register reads
    i = 0
    for regtype, regid in regs:
        if hex_common.is_read(regid):
            analyze_read(f, tag, regtype, regid, i)
        i += 1

    ## Analyze the register writes
    i = 0
    for regtype, regid in regs:
        if hex_common.is_written(regid):
            analyze_write(f, tag, regtype, regid, i)
        i += 1

    f.write("}\n\n")


def main():
    hex_common.read_semantics_file(sys.argv[1])
    hex_common.read_attribs_file(sys.argv[2])
    hex_common.read_overrides_file(sys.argv[3])
    hex_common.read_overrides_file(sys.argv[4])
    ## Whether or not idef-parser is enabled is
    ## determined by the number of arguments to
    ## this script:
    ##
    ##   5 args. -> not enabled,
    ##   6 args. -> idef-parser enabled.
    ##
    ## The 6:th arg. then holds a list of the successfully
    ## parsed instructions.
    is_idef_parser_enabled = len(sys.argv) > 6
    if is_idef_parser_enabled:
        hex_common.read_idef_parser_enabled_file(sys.argv[5])
    hex_common.calculate_attribs()
    tagregs = hex_common.get_tagregs()
    tagimms = hex_common.get_tagimms()

    with open(sys.argv[-1], "w") as f:
        f.write("#ifndef HEXAGON_ANALYZE_FUNCS_H\n")
        f.write("#define HEXAGON_ANALYZE_FUNCS_H\n\n")

        for tag in hex_common.tags:
            gen_analyze_func(f, tag, tagregs[tag], tagimms[tag])

        f.write("#endif    /* HEXAGON_ANALYZE_FUNCS_H */\n")


if __name__ == "__main__":
    main()
