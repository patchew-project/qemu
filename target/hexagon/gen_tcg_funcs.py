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
## Helpers for gen_tcg_func
##
def gen_decl_ea_tcg(f):
    f.write("DECL_EA;\n")

def gen_free_ea_tcg(f):
    f.write("FREE_EA;\n")

def genptr_decl(f,regtype,regid,regno):
    regN="%s%sN" % (regtype,regid)
    macro = "DECL_%sREG_%s" % (regtype, regid)
    f.write("%s(%s%sV, %s, %d, 0);\n" % \
        (macro, regtype, regid, regN, regno))

def genptr_decl_new(f,regtype,regid,regno):
    regN="%s%sX" % (regtype,regid)
    macro = "DECL_NEW_%sREG_%s" % (regtype, regid)
    f.write("%s(%s%sN, %s, %d, 0);\n" % \
        (macro, regtype, regid, regN, regno))

def genptr_decl_opn(f, tag, regtype, regid, toss, numregs, i):
    if (is_pair(regid)):
        genptr_decl(f,regtype,regid,i)
    elif (is_single(regid)):
        if is_old_val(regtype, regid, tag):
            genptr_decl(f,regtype,regid,i)
        elif is_new_val(regtype, regid, tag):
            genptr_decl_new(f,regtype,regid,i)
        else:
            print("Bad register parse: ",regtype,regid,toss,numregs)
    else:
        print("Bad register parse: ",regtype,regid,toss,numregs)

def genptr_decl_imm(f,immlett):
    if (immlett.isupper()):
        i = 1
    else:
        i = 0
    f.write("DECL_IMM(%s,%d);\n" % (imm_name(immlett),i))

def genptr_free(f,regtype,regid,regno):
    macro = "FREE_%sREG_%s" % (regtype, regid)
    f.write("%s(%s%sV);\n" % (macro, regtype, regid))

def genptr_free_new(f,regtype,regid,regno):
    macro = "FREE_NEW_%sREG_%s" % (regtype, regid)
    f.write("%s(%s%sN);\n" % (macro, regtype, regid))

def genptr_free_opn(f,regtype,regid,i,tag):
    if (is_pair(regid)):
        genptr_free(f,regtype,regid,i)
    elif (is_single(regid)):
        if is_old_val(regtype, regid, tag):
            genptr_free(f,regtype,regid,i)
        elif is_new_val(regtype, regid, tag):
            genptr_free_new(f,regtype,regid,i)
        else:
            print("Bad register parse: ",regtype,regid,toss,numregs)
    else:
        print("Bad register parse: ",regtype,regid,toss,numregs)

def genptr_free_imm(f,immlett):
    f.write("FREE_IMM(%s);\n" % (imm_name(immlett)))

def genptr_src_read(f,regtype,regid):
    macro = "READ_%sREG_%s" % (regtype, regid)
    f.write("%s(%s%sV, %s%sN);\n" % \
        (macro,regtype,regid,regtype,regid))

def genptr_src_read_new(f,regtype,regid):
    macro = "READ_NEW_%sREG_%s" % (regtype, regid)
    f.write("%s(%s%sN, %s%sX);\n" % \
        (macro,regtype,regid,regtype,regid))

def genptr_src_read_opn(f,regtype,regid,tag):
    if (is_pair(regid)):
        genptr_src_read(f,regtype,regid)
    elif (is_single(regid)):
        if is_old_val(regtype, regid, tag):
            genptr_src_read(f,regtype,regid)
        elif is_new_val(regtype, regid, tag):
            genptr_src_read_new(f,regtype,regid)
        else:
            print("Bad register parse: ",regtype,regid,toss,numregs)
    else:
        print("Bad register parse: ",regtype,regid,toss,numregs)

def gen_helper_call_opn(f, tag, regtype, regid, toss, numregs, i):
    if (i > 0): f.write(", ")
    if (is_pair(regid)):
        f.write("%s%sV" % (regtype,regid))
    elif (is_single(regid)):
        if is_old_val(regtype, regid, tag):
            f.write("%s%sV" % (regtype,regid))
        elif is_new_val(regtype, regid, tag):
            f.write("%s%sN" % (regtype,regid))
        else:
            print("Bad register parse: ",regtype,regid,toss,numregs)
    else:
        print("Bad register parse: ",regtype,regid,toss,numregs)

def gen_helper_decl_imm(f,immlett):
    f.write("DECL_TCG_IMM(tcgv_%s, %s);\n" % \
        (imm_name(immlett), imm_name(immlett)))

def gen_helper_call_imm(f,immlett):
    f.write(", tcgv_%s" % imm_name(immlett))

def gen_helper_free_imm(f,immlett):
    f.write("FREE_TCG_IMM(tcgv_%s);\n" % imm_name(immlett))

def genptr_dst_write(f,regtype, regid):
    macro = "WRITE_%sREG_%s" % (regtype, regid)
    f.write("%s(%s%sN, %s%sV);\n" % (macro, regtype, regid, regtype, regid))

def genptr_dst_write_opn(f,regtype, regid, tag):
    if (is_pair(regid)):
        genptr_dst_write(f, regtype, regid)
    elif (is_single(regid)):
        genptr_dst_write(f, regtype, regid)
    else:
        print("Bad register parse: ",regtype,regid,toss,numregs)

##
## Generate the TCG code to call the helper
##     For A2_add: Rd32=add(Rs32,Rt32), { RdV=RsV+RtV;}
##     We produce:
##       {
##       /* A2_add */
##       DECL_RREG_d(RdV, RdN, 0, 0);
##       DECL_RREG_s(RsV, RsN, 1, 0);
##       DECL_RREG_t(RtV, RtN, 2, 0);
##       READ_RREG_s(RsV, RsN);
##       READ_RREG_t(RtV, RtN);
##       #ifdef fGEN_TCG_A2_add
##       fGEN_TCG_A2_add({ RdV=RsV+RtV;});
##       #else
##       gen_helper_A2_add(RdV, cpu_env, RsV, RtV);
##       #endif
##       WRITE_RREG_d(RdN, RdV);
##       FREE_RREG_d(RdV);
##       FREE_RREG_s(RsV);
##       FREE_RREG_t(RtV);
##       /* A2_add */
##       }
##
def gen_tcg_func(f, tag, regs, imms):
    f.write('{\n')
    f.write('/* %s */\n' % tag)
    if need_ea(tag): gen_decl_ea_tcg(f)
    i=0
    ## Declare all the operands (regs and immediates)
    for regtype,regid,toss,numregs in regs:
        genptr_decl_opn(f, tag, regtype, regid, toss, numregs, i)
        i += 1
    for immlett,bits,immshift in imms:
        genptr_decl_imm(f,immlett)

    if 'A_PRIV' in attribdict[tag]:
        f.write('fCHECKFORPRIV();\n')
    if 'A_GUEST' in attribdict[tag]:
        f.write('fCHECKFORGUEST();\n')
    if 'A_FPOP' in attribdict[tag]:
        f.write('fFPOP_START();\n');

    ## Read all the inputs
    for regtype,regid,toss,numregs in regs:
        if (is_read(regid)):
            genptr_src_read_opn(f,regtype,regid,tag)

    f.write("#ifdef fGEN_TCG_%s\n" % tag)
    f.write("fGEN_TCG_%s(%s);\n" % (tag, semdict[tag]))
    f.write("#else\n")
    ## Generate the call to the helper
    f.write("do {\n")
    for immlett,bits,immshift in imms:
        gen_helper_decl_imm(f,immlett)
    if need_part1(tag): f.write("PART1_WRAP(")
    if need_slot(tag): f.write("SLOT_WRAP(")
    f.write("gen_helper_%s(" % (tag))
    i=0
    ## If there is a scalar result, it is the return type
    for regtype,regid,toss,numregs in regs:
        if (is_written(regid)):
            gen_helper_call_opn(f, tag, regtype, regid, toss, numregs, i)
            i += 1
    if (i > 0): f.write(", ")
    f.write("cpu_env")
    i=1
    for regtype,regid,toss,numregs in regs:
        if (is_read(regid)):
            gen_helper_call_opn(f, tag, regtype, regid, toss, numregs, i)
            i += 1
    for immlett,bits,immshift in imms:
        gen_helper_call_imm(f,immlett)

    if need_slot(tag): f.write(", slot")
    if need_part1(tag): f.write(", part1" )
    f.write(")")
    if need_slot(tag): f.write(")")
    if need_part1(tag): f.write(")")
    f.write(";\n")
    for immlett,bits,immshift in imms:
        gen_helper_free_imm(f,immlett)
    f.write("} while (0);\n")
    f.write("#endif\n")

    ## Write all the outputs
    for regtype,regid,toss,numregs in regs:
        if (is_written(regid)):
            genptr_dst_write_opn(f,regtype, regid, tag)

    if 'A_FPOP' in attribdict[tag]:
        f.write('fFPOP_END();\n');


    ## Free all the operands (regs and immediates)
    if need_ea(tag): gen_free_ea_tcg(f)
    for regtype,regid,toss,numregs in regs:
        genptr_free_opn(f,regtype,regid,i,tag)
        i += 1
    for immlett,bits,immshift in imms:
        genptr_free_imm(f,immlett)

    f.write("/* %s */\n" % tag)
    f.write("}")

def gen_def_tcg_func(f, tag, tagregs, tagimms):
    regs = tagregs[tag]
    imms = tagimms[tag]

    f.write('DEF_TCG_FUNC(%s, /* %s */\n' % (tag,semdict[tag]))
    gen_tcg_func(f, tag, regs, imms)
    f.write(")\n" )

def main():
    read_semantics_file(sys.argv[1])
    read_attribs_file(sys.argv[2])
    calculate_attribs()
    tagregs = get_tagregs()
    tagimms = get_tagimms()

    f = StringIO()

    f.write("#ifndef DEF_TCG_FUNC\n")
    f.write("#define DEF_TCG_FUNC(TAG,GENFN)         /* Nothing */\n")
    f.write("#endif\n")

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

        gen_def_tcg_func(f, tag, tagregs, tagimms)

    f.write("#undef DEF_TCG_FUNC\n")

    realf = open('tcg_funcs_generated.h','w')
    realf.write(f.getvalue())
    realf.close()
    f.close()

if __name__ == "__main__":
    main()
