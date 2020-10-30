#!/usr/bin/env python3

##
##  Copyright(c) 2019-2020 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
from io import StringIO

from hex_common import *

##
## Helpers for gen_helper_prototype
##
def_helper_types = {
    'N' : 's32',
    'O' : 's32',
    'P' : 's32',
    'M' : 's32',
    'C' : 's32',
    'R' : 's32',
    'V' : 'ptr',
    'Q' : 'ptr'
}

def_helper_types_pair = {
    'R' : 's64',
    'C' : 's64',
    'S' : 's64',
    'G' : 's64',
    'V' : 'ptr',
    'Q' : 'ptr'
}

def gen_def_helper_opn(f, tag, regtype, regid, toss, numregs, i):
    if (is_pair(regid)):
        f.write(", %s" % (def_helper_types_pair[regtype]))
    elif (is_single(regid)):
        f.write(", %s" % (def_helper_types[regtype]))
    else:
        print("Bad register parse: ",regtype,regid,toss,numregs)

##
## Generate the DEF_HELPER prototype for an instruction
##     For A2_add: Rd32=add(Rs32,Rt32)
##     We produce:
##         DEF_HELPER_3(A2_add, s32, env, s32, s32)
##
def gen_helper_prototype(f, tag, tagregs, tagimms):
    regs = tagregs[tag]
    imms = tagimms[tag]

    numresults = 0
    numscalarresults = 0
    numscalarreadwrite = 0
    for regtype,regid,toss,numregs in regs:
        if (is_written(regid)):
            numresults += 1
            if (is_scalar_reg(regtype)):
                numscalarresults += 1
        if (is_readwrite(regid)):
            if (is_scalar_reg(regtype)):
                numscalarreadwrite += 1

    if (numscalarresults > 1):
        ## The helper is bogus when there is more than one result
        f.write('DEF_HELPER_1(%s, void, env)\n' % tag)
    else:
        ## Figure out how many arguments the helper will take
        if (numscalarresults == 0):
            def_helper_size = len(regs)+len(imms)+numscalarreadwrite+1
            if need_part1(tag): def_helper_size += 1
            if need_slot(tag): def_helper_size += 1
            f.write('DEF_HELPER_%s(%s' % (def_helper_size, tag))
            ## The return type is void
            f.write(', void' )
        else:
            def_helper_size = len(regs)+len(imms)+numscalarreadwrite
            if need_part1(tag): def_helper_size += 1
            if need_slot(tag): def_helper_size += 1
            f.write('DEF_HELPER_%s(%s' % (def_helper_size, tag))

        ## Generate the qemu DEF_HELPER type for each result
        i=0
        for regtype,regid,toss,numregs in regs:
            if (is_written(regid)):
                gen_def_helper_opn(f, tag, regtype, regid, toss, numregs, i)
                i += 1

        ## Put the env between the outputs and inputs
        f.write(', env' )
        i += 1

        ## Generate the qemu type for each input operand (regs and immediates)
        for regtype,regid,toss,numregs in regs:
            if (is_read(regid)):
                gen_def_helper_opn(f, tag, regtype, regid, toss, numregs, i)
                i += 1
        for immlett,bits,immshift in imms:
            f.write(", s32")

        ## Add the arguments for the instruction slot and part1 (if needed)
        if need_slot(tag): f.write(', i32' )
        if need_part1(tag): f.write(' , i32' )
        f.write(')\n')

def main():
    read_semantics_file(sys.argv[1])
    read_attribs_file(sys.argv[2])
    read_overrides_file(sys.argv[3])
    calculate_attribs()
    tagregs = get_tagregs()
    tagimms = get_tagimms()

    f = StringIO()

    for tag in tags:
        ## Skip the priv instructions
        if ( "A_PRIV" in attribdict[tag] ) :
            continue
        ## Skip the guest instructions
        if ( "A_GUEST" in attribdict[tag] ) :
            continue
        ## Skip the diag instructions
        if ( tag == "Y6_diag" ) :
            continue
        if ( tag == "Y6_diag0" ) :
            continue
        if ( tag == "Y6_diag1" ) :
            continue

        if ( skip_qemu_helper(tag) ):
            continue

        gen_helper_prototype(f, tag, tagregs, tagimms)

    realf = open(sys.argv[4], 'w')
    realf.write(f.getvalue())
    realf.close()
    f.close()

if __name__ == "__main__":
    main()
