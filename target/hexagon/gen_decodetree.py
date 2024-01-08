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

num_registers = {"R": 32, "V": 32}

operand_letters = {
    "P",
    "i",
    "I",
    "r",
    "s",
    "t",
    "u",
    "v",
    "w",
    "x",
    "y",
    "z",
    "d",
    "e",
    "f",
    "g"
}

#
# These instructions have unused operand letters in their encoding
# They don't correspond to actual operands in the instruction semantics
# We will mark them as ignored in QEMU decodetree
#
tags_with_unused_d_encoding = {
    "R6_release_at_vi",
    "R6_release_st_vi",
    "S4_stored_rl_at_vi",
    "S4_stored_rl_st_vi",
    "S2_storew_rl_at_vi",
    "S2_stored_rl_at_vi",
    "S2_storew_rl_st_vi",
}

tags_with_unused_t_encoding = {
    "R6_release_at_vi",
    "R6_release_st_vi",
}

def skip_tag(tag, class_to_decode):
    enc_class = iset.iset[tag]["enc_class"]
    return enc_class != class_to_decode


##
## Generate the QEMU decodetree file for each instruction in class_to_decode
##     For A2_add: Rd32=add(Rs32,Rt32)
##     We produce:
##     %A2_add_Rd   0:5
##     %A2_add_Rs   16:5
##     %A2_add_Rt   8:5
##     @A2_add  11110011000.......-.....---..... Rd=%A2_add_Rd Rs=%A2_add_Rs Rt=%A2_add_Rt %PP
##     A2_add   ..................-.....---..... @A2_add
##
def gen_decodetree_file(f, class_to_decode):
    f.write(f"## DO NOT MODIFY - This file is generated by {sys.argv[0]}\n\n")
    f.write("%PP\t14:2\n\n")
    for tag in sorted(encs.keys(), key=iset.tags.index):
        if skip_tag(tag, class_to_decode):
            continue

        f.write("########################################")
        f.write("########################################\n")

        enc = encs[tag]
        enc_str = "".join(reversed(encs[tag]))
        f.write(f"## {tag}:\t{enc_str}\n")
        f.write("##\n")

        regs = ordered_unique(regre.findall(iset.iset[tag]["syntax"]))
        imms = ordered_unique(immre.findall(iset.iset[tag]["syntax"]))

        # Write the field definitions for the registers
        regno = 0
        for reg in regs:
            reg_type = reg[0]
            reg_id = reg[1]
            reg_letter = reg_id[0]
            reg_num_choices = int(reg[3].rstrip("S"))
            reg_mapping = reg[0] + "".join(["_" for letter in reg[1]]) + reg[3]
            reg_enc_fields = re.findall(reg_letter + "+", enc)

            # Check for some errors
            if len(reg_enc_fields) == 0:
                raise Exception(f"{tag} missing register field!")
            if len(reg_enc_fields) > 1:
                raise Exception(f"{tag} has split register field!")
            reg_enc_field = reg_enc_fields[0]
            if 2 ** len(reg_enc_field) != reg_num_choices:
                raise Exception(f"{tag} has incorrect register field width!")

            f.write(f"%{tag}_{reg_type}{reg_id}\t")
            f.write(f"{enc.index(reg_enc_field)}:{len(reg_enc_field)}")
            if (reg_type in num_registers and
                reg_num_choices != num_registers[reg_type]):
                f.write(f"\t!function=decode_mapped_reg_{reg_mapping}")
            f.write("\n")
            regno += 1

        # Write the field definitions for the immediates
        for imm in imms:
            immno = 1 if imm[0].isupper() else 0
            imm_type = imm[0]
            imm_width = int(imm[1])
            imm_letter = "i" if imm_type.islower() else "I"
            fields = []
            sign_mark = "s" if imm_type.lower() in "sr" else ""
            for m in reversed(list(re.finditer(imm_letter + "+", enc))):
                fields.append(f"{m.start()}:{sign_mark}{m.end() - m.start()}")
                sign_mark = ""
            field_str = " ".join(fields)
            f.write(f"%{tag}_{imm_type}{imm_letter}\t{field_str}\n")

        ## Handle instructions with unused encoding letters
        ## Change the unused letters to ignored
        if tag in tags_with_unused_d_encoding:
            enc_str = enc_str.replace("d", "-")
        if tag in tags_with_unused_t_encoding:
            enc_str = enc_str.replace("t", "-")

        # Replace the operand letters with .
        for x in operand_letters:
            enc_str = enc_str.replace(x, ".")

        # Write the instruction format
        f.write(f"@{tag}\t{enc_str}")
        for reg in regs:
            reg_type = reg[0]
            reg_id = reg[1]
            f.write(f" {reg_type}{reg_id}=%{tag}_{reg_type}{reg_id}")
        for imm in imms:
            imm_type = imm[0]
            imm_letter = "i" if imm_type.islower() else "I"
            f.write(f" {imm_type}{imm_letter}=%{tag}_{imm_type}{imm_letter}")

        f.write(" %PP\n")

         # Replace the 0s and 1s with .
        for x in { "0", "1" }:
            enc_str = enc_str.replace(x, ".")

        # Write the instruction pattern
        f.write(f"{tag}\t{enc_str} @{tag}\n")


if __name__ == "__main__":
    hex_common.read_semantics_file(sys.argv[1])
    class_to_decode = sys.argv[2]
    with open(sys.argv[3], "w") as f:
        gen_decodetree_file(f, class_to_decode)
