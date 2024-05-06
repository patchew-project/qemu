#!/usr/bin/env python3

##
##  Copyright(c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
## Generate the code to analyze the instruction
##     For A2_add: Rd32=add(Rs32,Rt32), { RdV=RsV+RtV;}
##     We produce:
##     static void analyze_A2_add(DisasContext *ctx)
##     {
##         Insn *insn G_GNUC_UNUSED = ctx->insn;
##         const int RdN = insn->regno[0];
##         ctx_log_reg_write(ctx, RdN, false);
##         const int RsN = insn->regno[1];
##         ctx_log_reg_read(ctx, RsN);
##         const int RtN = insn->regno[2];
##         ctx_log_reg_read(ctx, RtN);
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

    ## Declare all the registers
    for regno, register in enumerate(regs):
        reg_type, reg_id = register
        reg = hex_common.get_register(tag, reg_type, reg_id)
        reg.decl_reg_num(f, regno)

    ## Analyze the register reads
    for regno, register in enumerate(regs):
        reg_type, reg_id = register
        reg = hex_common.get_register(tag, reg_type, reg_id)
        if reg.is_read():
            reg.analyze_read(f, regno)

    ## Analyze the register writes
    for regno, register in enumerate(regs):
        reg_type, reg_id = register
        reg = hex_common.get_register(tag, reg_type, reg_id)
        if reg.is_written():
            reg.analyze_write(f, tag, regno)

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
    hex_common.init_registers()
    tagregs = hex_common.get_tagregs()
    tagimms = hex_common.get_tagimms()

    with open(sys.argv[-1], "w") as f:
        f.write("#ifndef HEXAGON_ANALYZE_FUNCS_C_INC\n")
        f.write("#define HEXAGON_ANALYZE_FUNCS_C_INC\n\n")

        for tag in hex_common.tags:
            gen_analyze_func(f, tag, tagregs[tag], tagimms[tag])

        f.write("#endif    /* HEXAGON_ANALYZE_FUNCS_C_INC */\n")


if __name__ == "__main__":
    main()
