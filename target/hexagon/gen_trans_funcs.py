#!/usr/bin/env python3

##
##  Copyright (c) 2024 Taylor Simpson <ltaylorsimpson@gmail.com>
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

import io
import re

import sys
import textwrap
import iset
import hex_common

encs = {
    tag: "".join(reversed(iset.iset[tag]["enc"].replace(" ", "")))
    for tag in iset.tags
    if iset.iset[tag]["enc"] != "MISSING ENCODING"
}


regre = re.compile(r"((?<!DUP)[MNORCPQXSGVZA])([stuvwxyzdefg]+)([.]?[LlHh]?)(\d+S?)")
immre = re.compile(r"[#]([rRsSuUm])(\d+)(?:[:](\d+))?")


def ordered_unique(l):
    return sorted(set(l), key=l.index)


def code_fmt(txt):
    return textwrap.indent(textwrap.dedent(txt), "    ")

open_curly = "{"
close_curly = "}"

def mark_which_imm_extended(f, tag):
    immre = re.compile(r"IMMEXT\([rRsSuUm]")
    imm = immre.findall(hex_common.semdict[tag])
    if len(imm) == 0:
        # No extended operand found
        return
    letter = re.split("\\(", imm[0])[1]
    f.write(code_fmt(f"""\
        insn->which_extended = {0 if letter.islower() else 1};
    """))

##
## Generate the QEMU decodetree trans_<tag> function for each instruction
##     For A2_add: Rd32=add(Rs32,Rt32)
##     We produce:
##     static bool trans_A2_add(DisasContext *ctx, arg_A2_add *args)
##     {
##         Insn *insn = ctx->insn;
##         insn->opcode = A2_add;
##         insn->regno[0] = args->Rd;
##         insn->regno[1] = args->Rs;
##         insn->regno[2] = args->Rt;
##         insn->new_read_idx = -1;
##         insn->dest_idx = 0;
##         insn->has_pred_dest = false;
##         return true;
##     }
##
def gen_trans_funcs(f):
    f.write(f"/* DO NOT MODIFY - This file is generated by {sys.argv[0]} */\n\n")
    for tag in sorted(encs.keys(), key=iset.tags.index):
        regs = ordered_unique(regre.findall(iset.iset[tag]["syntax"]))
        imms = ordered_unique(immre.findall(iset.iset[tag]["syntax"]))

        f.write(textwrap.dedent(f"""\
            static bool trans_{tag}(DisasContext *ctx, arg_{tag} *args)
            {open_curly}
                Insn *insn = ctx->insn;
                insn->opcode = {tag};
        """))

        new_read_idx = -1
        dest_idx = -1
        has_pred_dest = "false"
        for regno, (reg_type, reg_id, *_) in enumerate(regs):
            reg = hex_common.get_register(tag, reg_type, reg_id)
            f.write(code_fmt(f"""\
                insn->regno[{regno}] = args->{reg_type}{reg_id};
            """))
            if reg.is_read() and reg.is_new():
                new_read_idx = regno
            # dest_idx should be the first destination, so check for -1
            if reg.is_written() and dest_idx == -1:
                dest_idx = regno
            if reg_type == "P" and reg.is_written() and not reg.is_read():
                has_pred_dest = "true"

        if len(imms) != 0:
            mark_which_imm_extended(f, tag)

        for imm in imms:
            imm_type = imm[0]
            imm_letter = "i" if imm_type.islower() else "I"
            immno = 0 if imm_type.islower() else 1
            imm_shift = int(imm[2]) if imm[2] else 0
            if imm_shift:
                f.write(code_fmt(f"""\
                    insn->immed[{immno}] =
                        shift_left(ctx, args->{imm_type}{imm_letter},
                                   {imm_shift}, {immno});
                """))
            else:
                f.write(code_fmt(f"""\
                    insn->immed[{immno}] = args->{imm_type}{imm_letter};
                """))

        f.write(code_fmt(f"""\
            insn->new_read_idx = {new_read_idx};
            insn->dest_idx = {dest_idx};
            insn->has_pred_dest = {has_pred_dest};
        """))
        f.write(textwrap.dedent(f"""\
                return true;
            {close_curly}
        """))


if __name__ == "__main__":
    hex_common.read_semantics_file(sys.argv[1])
    hex_common.init_registers()
    with open(sys.argv[2], "w") as f:
        gen_trans_funcs(f)
