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

    i = 0
    ## Analyze all the registers
    for regtype, regid in regs:
        reg = hex_common.get_register(tag, regtype, regid)
        if reg.is_written():
            reg.analyze_write(f, tag, i)
        else:
            reg.analyze_read(f, i)
        i += 1

    has_generated_helper = not hex_common.skip_qemu_helper(
        tag
    ) and not hex_common.is_idef_parser_enabled(tag)

    ## Mark HVX instructions with generated helpers
    if (has_generated_helper and
        "A_CVI" in hex_common.attribdict[tag]):
        f.write("    ctx->has_hvx_helper = true;\n")

    f.write("}\n\n")


def main():
    hex_common.read_common_files()
    tagregs = hex_common.get_tagregs()
    tagimms = hex_common.get_tagimms()

    with open(sys.argv[-1], "w") as f:
        f.write("#ifndef HEXAGON_TCG_FUNCS_H\n")
        f.write("#define HEXAGON_TCG_FUNCS_H\n\n")

        for tag in hex_common.tags:
            gen_analyze_func(f, tag, tagregs[tag], tagimms[tag])

        f.write("#endif    /* HEXAGON_TCG_FUNCS_H */\n")


if __name__ == "__main__":
    main()
