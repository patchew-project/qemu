#!/usr/bin/env python3

##
##  Copyright(c) 2019-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
## Generate the DEF_HELPER prototype for an instruction
##     For A2_add: Rd32=add(Rs32,Rt32)
##     We produce:
##         DEF_HELPER_3(A2_add, s32, env, s32, s32)
##
def gen_helper_prototype(f, tag, tagregs, tagimms):
    regs = tagregs[tag]
    imms = tagimms[tag]

    ## If there is a scalar result, it is the return type
    return_type = ""
    numscalarresults = 0
    for regtype, regid in regs:
        reg = hex_common.get_register(tag, regtype, regid)
        if reg.is_written() and reg.is_scalar_reg():
                return_type = reg.helper_proto_type()
                numscalarresults += 1
    if numscalarresults == 0:
        return_type = "void"

    if numscalarresults > 1:
        raise Exception("numscalarresults > 1")

    declared = []
    declared.append(return_type)

    ## Put the env between the outputs and inputs
    declared.append("env")

    ## For predicated instructions, we pass in the destination register
    if hex_common.is_predicated(tag):
        for regtype, regid in regs:
            reg = hex_common.get_register(tag, regtype, regid)
            if reg.is_writeonly() and not reg.is_hvx_reg():
                declared.append(reg.helper_proto_type())
    ## Pass the HVX destination registers
    for regtype, regid in regs:
        reg = hex_common.get_register(tag, regtype, regid)
        if reg.is_written() and reg.is_hvx_reg():
            declared.append(reg.helper_proto_type())
    ## Pass the source registers
    for regtype, regid in regs:
        reg = hex_common.get_register(tag, regtype, regid)
        if reg.is_read() and not (reg.is_hvx_reg() and reg.is_readwrite()):
            declared.append(reg.helper_proto_type())
    ## Pass the immediates
    for immlett, bits, immshift in imms:
        declared.append("s32")

    ## Other stuff the helper might need
    if hex_common.need_pkt_has_multi_cof(tag):
        declared.append("i32")
    if hex_common.need_pkt_need_commit(tag):
        declared.append("i32")
    if hex_common.need_PC(tag):
        declared.append("i32")
    if hex_common.need_next_PC(tag):
        declared.append("i32")
    if hex_common.need_slot(tag):
        declared.append("i32")
    if hex_common.need_part1(tag):
        declared.append("i32")

    arguments = ", ".join(declared)
    f.write(f"DEF_HELPER_{len(declared) - 1}({tag}, {arguments})\n")


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

    output_file = sys.argv[-1]
    with open(output_file, "w") as f:
        for tag in hex_common.tags:
            ## Skip the priv instructions
            if "A_PRIV" in hex_common.attribdict[tag]:
                continue
            ## Skip the guest instructions
            if "A_GUEST" in hex_common.attribdict[tag]:
                continue
            ## Skip the diag instructions
            if tag == "Y6_diag":
                continue
            if tag == "Y6_diag0":
                continue
            if tag == "Y6_diag1":
                continue

            if hex_common.skip_qemu_helper(tag):
                continue
            if hex_common.is_idef_parser_enabled(tag):
                continue

            gen_helper_prototype(f, tag, tagregs, tagimms)


if __name__ == "__main__":
    main()
