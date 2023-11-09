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

behdict = {}  # tag ->behavior
semdict = {}  # tag -> semantics
attribdict = {}  # tag -> attributes
macros = {}  # macro -> macro information...
attribinfo = {}  # Register information and misc
registers = {}  # register -> register functions
new_registers = {}
tags = []  # list of all tags
overrides = {}  # tags with helper overrides
idef_parser_enabled = {}  # tags enabled for idef-parser

def bad_register(regtype, regid):
    raise Exception(f"Bad register parse: regtype '{regtype}' regid '{regid}'")

# We should do this as a hash for performance,
# but to keep order let's keep it as a list.
def uniquify(seq):
    seen = set()
    seen_add = seen.add
    return [x for x in seq if x not in seen and not seen_add(x)]


regre = re.compile(r"((?<!DUP)[MNORCPQXSGVZA])([stuvwxyzdefg]+)([.]?[LlHh]?)(\d+S?)")
immre = re.compile(r"[#]([rRsSuUm])(\d+)(?:[:](\d+))?")
reg_or_immre = re.compile(
    r"(((?<!DUP)[MNRCOPQXSGVZA])([stuvwxyzdefg]+)"
    r"([.]?[LlHh]?)(\d+S?))|([#]([rRsSuUm])(\d+)[:]?(\d+)?)"
)
relimmre = re.compile(r"[#]([rR])(\d+)(?:[:](\d+))?")
absimmre = re.compile(r"[#]([sSuUm])(\d+)(?:[:](\d+))?")

finished_macros = set()


def expand_macro_attribs(macro, allmac_re):
    if macro.key not in finished_macros:
        # Get a list of all things that might be macros
        l = allmac_re.findall(macro.beh)
        for submacro in l:
            if not submacro:
                continue
            if not macros[submacro]:
                raise Exception(f"Couldn't find macro: <{l}>")
            macro.attribs |= expand_macro_attribs(macros[submacro], allmac_re)
            finished_macros.add(macro.key)
    return macro.attribs


# When qemu needs an attribute that isn't in the imported files,
# we'll add it here.
def add_qemu_macro_attrib(name, attrib):
    macros[name].attribs.add(attrib)


immextre = re.compile(r"f(MUST_)?IMMEXT[(]([UuSsRr])")


def is_cond_jump(tag):
    if tag == "J2_rte":
        return False
    if "A_HWLOOP0_END" in attribdict[tag] or "A_HWLOOP1_END" in attribdict[tag]:
        return False
    return re.compile(r"(if.*fBRANCH)|(if.*fJUMPR)").search(semdict[tag]) != None


def is_cond_call(tag):
    return re.compile(r"(if.*fCALL)").search(semdict[tag]) != None


def calculate_attribs():
    add_qemu_macro_attrib("fREAD_PC", "A_IMPLICIT_READS_PC")
    add_qemu_macro_attrib("fTRAP", "A_IMPLICIT_READS_PC")
    add_qemu_macro_attrib("fWRITE_P0", "A_WRITES_PRED_REG")
    add_qemu_macro_attrib("fWRITE_P1", "A_WRITES_PRED_REG")
    add_qemu_macro_attrib("fWRITE_P2", "A_WRITES_PRED_REG")
    add_qemu_macro_attrib("fWRITE_P3", "A_WRITES_PRED_REG")
    add_qemu_macro_attrib("fSET_OVERFLOW", "A_IMPLICIT_WRITES_USR")
    add_qemu_macro_attrib("fSET_LPCFG", "A_IMPLICIT_WRITES_USR")
    add_qemu_macro_attrib("fLOAD", "A_SCALAR_LOAD")
    add_qemu_macro_attrib("fSTORE", "A_SCALAR_STORE")
    add_qemu_macro_attrib('fLSBNEW0', 'A_IMPLICIT_READS_P0')
    add_qemu_macro_attrib('fLSBNEW0NOT', 'A_IMPLICIT_READS_P0')
    add_qemu_macro_attrib('fREAD_P0', 'A_IMPLICIT_READS_P0')
    add_qemu_macro_attrib('fLSBNEW1', 'A_IMPLICIT_READS_P1')
    add_qemu_macro_attrib('fLSBNEW1NOT', 'A_IMPLICIT_READS_P1')
    add_qemu_macro_attrib('fREAD_P3', 'A_IMPLICIT_READS_P3')

    # Recurse down macros, find attributes from sub-macros
    macroValues = list(macros.values())
    allmacros_restr = "|".join(set([m.re.pattern for m in macroValues]))
    allmacros_re = re.compile(allmacros_restr)
    for macro in macroValues:
        expand_macro_attribs(macro, allmacros_re)
    # Append attributes to all instructions
    for tag in tags:
        for macname in allmacros_re.findall(semdict[tag]):
            if not macname:
                continue
            macro = macros[macname]
            attribdict[tag] |= set(macro.attribs)
    # Figure out which instructions write predicate registers
    tagregs = get_tagregs()
    for tag in tags:
        regs = tagregs[tag]
        for regtype, regid in regs:
            if regtype == "P" and is_written(regid):
                attribdict[tag].add("A_WRITES_PRED_REG")
    # Mark conditional jumps and calls
    #     Not all instructions are properly marked with A_CONDEXEC
    for tag in tags:
        if is_cond_jump(tag) or is_cond_call(tag):
            attribdict[tag].add("A_CONDEXEC")


def SEMANTICS(tag, beh, sem):
    # print tag,beh,sem
    behdict[tag] = beh
    semdict[tag] = sem
    attribdict[tag] = set()
    tags.append(tag)  # dicts have no order, this is for order


def ATTRIBUTES(tag, attribstring):
    attribstring = attribstring.replace("ATTRIBS", "").replace("(", "").replace(")", "")
    if not attribstring:
        return
    attribs = attribstring.split(",")
    for attrib in attribs:
        attribdict[tag].add(attrib.strip())


class Macro(object):
    __slots__ = ["key", "name", "beh", "attribs", "re"]

    def __init__(self, name, beh, attribs):
        self.key = name
        self.name = name
        self.beh = beh
        self.attribs = set(attribs)
        self.re = re.compile("\\b" + name + "\\b")


def MACROATTRIB(macname, beh, attribstring):
    attribstring = attribstring.replace("(", "").replace(")", "")
    if attribstring:
        attribs = attribstring.split(",")
    else:
        attribs = []
    macros[macname] = Macro(macname, beh, attribs)

def compute_tag_regs(tag, full):
    tagregs = regre.findall(behdict[tag])
    if not full:
        tagregs = map(lambda reg: reg[:2], tagregs)
    return uniquify(tagregs)

def compute_tag_immediates(tag):
    return uniquify(immre.findall(behdict[tag]))


##
##  tagregs is the main data structure we'll use
##  tagregs[tag] will contain the registers used by an instruction
##  Within each entry, we'll use the regtype and regid fields
##      regtype can be one of the following
##          C                control register
##          N                new register value
##          P                predicate register
##          R                GPR register
##          M                modifier register
##          Q                HVX predicate vector
##          V                HVX vector register
##          O                HVX new vector register
##      regid can be one of the following
##          d, e             destination register
##          dd               destination register pair
##          s, t, u, v, w    source register
##          ss, tt, uu, vv   source register pair
##          x, y             read-write register
##          xx, yy           read-write register pair
##
def get_tagregs(full=False):
    compute_func = lambda tag: compute_tag_regs(tag, full)
    return dict(zip(tags, list(map(compute_func, tags))))

def get_tagimms():
    return dict(zip(tags, list(map(compute_tag_immediates, tags))))


def is_pair(regid):
    return len(regid) == 2


def is_single(regid):
    return len(regid) == 1


def is_written(regid):
    return regid[0] in "dexy"


def is_writeonly(regid):
    return regid[0] in "de"


def is_read(regid):
    return regid[0] in "stuvwxy"


def is_readwrite(regid):
    return regid[0] in "xy"


def is_scalar_reg(regtype):
    return regtype in "RPC"


def is_hvx_reg(regtype):
    return regtype in "VQ"


def is_old_val(regtype, regid, tag):
    return regtype + regid + "V" in semdict[tag]


def is_new_val(regtype, regid, tag):
    return regtype + regid + "N" in semdict[tag]


def need_slot(tag):
    if (
        "A_CVI_SCATTER" not in attribdict[tag]
        and "A_CVI_GATHER" not in attribdict[tag]
        and ("A_STORE" in attribdict[tag]
             or "A_LOAD" in attribdict[tag])
    ):
        return 1
    else:
        return 0


def need_part1(tag):
    return re.compile(r"fPART1").search(semdict[tag])


def need_ea(tag):
    return re.compile(r"\bEA\b").search(semdict[tag])


def need_PC(tag):
    return "A_IMPLICIT_READS_PC" in attribdict[tag]


def helper_needs_next_PC(tag):
    return "A_CALL" in attribdict[tag]


def need_pkt_has_multi_cof(tag):
    return "A_COF" in attribdict[tag]


def need_pkt_need_commit(tag):
    return 'A_IMPLICIT_WRITES_USR' in attribdict[tag]

def need_condexec_reg(tag, regs):
    if "A_CONDEXEC" in attribdict[tag]:
        for regtype, regid in regs:
            if is_writeonly(regid) and not is_hvx_reg(regtype):
                return True
    return False


def skip_qemu_helper(tag):
    return tag in overrides.keys()


def is_tmp_result(tag):
    return "A_CVI_TMP" in attribdict[tag] or "A_CVI_TMP_DST" in attribdict[tag]


def is_new_result(tag):
    return "A_CVI_NEW" in attribdict[tag]


def is_idef_parser_enabled(tag):
    return tag in idef_parser_enabled


def imm_name(immlett):
    return f"{immlett}iV"


def read_semantics_file(name):
    eval_line = ""
    for line in open(name, "rt").readlines():
        if not line.startswith("#"):
            eval_line += line
            if line.endswith("\\\n"):
                eval_line.rstrip("\\\n")
            else:
                eval(eval_line.strip())
                eval_line = ""


def read_attribs_file(name):
    attribre = re.compile(
        r"DEF_ATTRIB\(([A-Za-z0-9_]+), ([^,]*), "
        + r'"([A-Za-z0-9_\.]*)", "([A-Za-z0-9_\.]*)"\)'
    )
    for line in open(name, "rt").readlines():
        if not attribre.match(line):
            continue
        (attrib_base, descr, rreg, wreg) = attribre.findall(line)[0]
        attrib_base = "A_" + attrib_base
        attribinfo[attrib_base] = {"rreg": rreg, "wreg": wreg, "descr": descr}


def read_overrides_file(name):
    overridere = re.compile(r"#define fGEN_TCG_([A-Za-z0-9_]+)\(.*")
    for line in open(name, "rt").readlines():
        if not overridere.match(line):
            continue
        tag = overridere.findall(line)[0]
        overrides[tag] = True


def read_idef_parser_enabled_file(name):
    global idef_parser_enabled
    with open(name, "r") as idef_parser_enabled_file:
        lines = idef_parser_enabled_file.read().strip().split("\n")
        idef_parser_enabled = set(lines)


def hvx_newv(tag):
    if is_new_result(tag):
        return "EXT_NEW"
    elif is_tmp_result(tag):
        return "EXT_TMP"
    else:
        return "EXT_DFL"

class Register:
    def __init__(self, regtype, regid):
        self.regtype = regtype
        self.regid = regid
        self.regN = f"{regtype}{regid}N"
        self.regV = f"{regtype}{regid}V"
    def is_scalar_reg(self):
        return True
    def is_hvx_reg(self):
        return False
    def idef_arg(self, declared):
        declared.append(self.regV)

class Dest(Register):
    def is_written(self):
        return True
    def is_writeonly(self):
        return True
    def is_read(self):
        return False
    def is_readwrite(self):
        return False

class Source(Register):
    def is_written(self):
        return False
    def is_writeonly(self):
        return False
    def is_read(self):
        return True
    def is_readwrite(self):
        return False

class ReadWrite(Register):
    def is_written(self):
        return True
    def is_writeonly(self):
        return False
    def is_read(self):
        return True
    def is_readwrite(self):
        return True

class GprDest(Dest):
    def genptr_decl(self, f, tag, regno):
        f.write(f"    const int {self.regN} = insn->regno[{regno}];\n")
        f.write(f"    TCGv {self.regV} = get_result_gpr(ctx, {self.regN});\n")
    def genptr_dst_write(self, f, tag):
        f.write(f"    gen_log_reg_write(ctx, {self.regN}, {self.regV});\n")

class GprSource(Source):
    def genptr_decl(self, f, tag, regno):
        f.write(f"    const int {self.regN} = insn->regno[{regno}];\n")
        f.write(f"    TCGv {self.regV} = hex_gpr[{self.regN}];\n")

class GprNewSource(Source):
    def genptr_decl(self, f, tag, regno):
        self.regV = self.regN
        f.write(
            f"    TCGv {self.regV} = "
            f"get_result_gpr(ctx, insn->regno[{regno}]);\n"
        )

class GprReadWrite(ReadWrite):
    def genptr_decl(self, f, tag, regno):
        f.write(f"    const int {self.regN} = insn->regno[{regno}];\n")
        f.write(f"    TCGv {self.regV} = get_result_gpr(ctx, {self.regN});\n")
        ## For read/write registers, we need to get the original value into
        ## the result TCGv.  For conditional instructions, this is done in
        ## gen_start_packet.  For unconditional instructions, we do it here.
        if "A_CONDEXEC" not in attribdict[tag]:
            f.write(f"    tcg_gen_mov_tl({self.regV}, hex_gpr[{self.regN}]);\n")
    def genptr_dst_write(self, f, tag):
        f.write(f"    gen_log_reg_write(ctx, {self.regN}, {self.regV});\n")

class ControlDest(Dest):
    def genptr_decl(self, f, tag, regno):
        f.write(
            f"    const int {self.regN} = "
            f"insn->regno[{regno}]  + HEX_REG_SA0;\n"
        )
        f.write(f"    TCGv {self.regV} = get_result_gpr(ctx, {self.regN});\n")
    def genptr_dst_write(self, f, tag):
        f.write(f"    gen_write_ctrl_reg(ctx, {self.regN}, {self.regV});\n")

class ControlSource(Source):
    def genptr_decl(self, f, tag, regno):
        f.write(
            f"    const int {self.regN} = "
            f"insn->regno[{regno}] + HEX_REG_SA0;\n"
        )
        f.write(f"    TCGv {self.regV} = tcg_temp_new();\n")
        f.write(f"    gen_read_ctrl_reg(ctx, {self.regN}, {self.regV});\n")

class ModifierSource(Source):
    def genptr_decl(self, f, tag, regno):
        f.write(f"    const int {self.regN} = insn->regno[{regno}];\n")
        f.write(f"    TCGv {self.regV} = hex_gpr[{self.regN} + HEX_REG_M0];\n")
    def idef_arg(self, declared):
        declared.append(self.regV)
        declared.append(self.regN)

class PredDest(Dest):
    def genptr_decl(self, f, tag, regno):
        f.write(f"    const int {self.regN} = insn->regno[{regno}];\n")
        f.write(f"    TCGv {self.regV} = tcg_temp_new();\n")
    def genptr_dst_write(self, f, tag):
        f.write(f"    gen_log_pred_write(ctx, {self.regN}, {self.regV});\n")

class PredSource(Source):
    def genptr_decl(self, f, tag, regno):
        f.write(f"    const int {self.regN} = insn->regno[{regno}];\n")
        f.write(f"    TCGv {self.regV} = hex_pred[{self.regN}];\n")

class PredNewSource(Source):
    def genptr_decl(self, f, tag, regno):
        self.regV = self.regN
        f.write(
            f"    TCGv {self.regV} = "
            f"get_result_pred(ctx, insn->regno[{regno}]);\n"
        )

class PredReadWrite(ReadWrite):
    def genptr_decl(self, f, tag, regno):
        f.write(f"    const int {self.regN} = insn->regno[{regno}];\n")
        f.write(f"    TCGv {self.regV} = tcg_temp_new();\n")
        f.write(f"    tcg_gen_mov_tl({self.regV}, hex_pred[{self.regN}]);\n")
    def genptr_dst_write(self, f, tag):
        f.write(f"    gen_log_pred_write(ctx, {self.regN}, {self.regV});\n")

class PairDest(Dest):
    def genptr_decl(self, f, tag, regno):
        f.write(f"    const int {self.regN} = insn->regno[{regno}];\n")
        f.write(
            f"    TCGv_i64 {self.regV} = "
            f"get_result_gpr_pair(ctx, {self.regN});\n"
        )
    def genptr_dst_write(self, f, tag):
        f.write(f"    gen_log_reg_write_pair(ctx, {self.regN}, {self.regV});\n")

class PairSource(Source):
    def genptr_decl(self, f, tag, regno):
        f.write(f"    const int {self.regN} = insn->regno[{regno}];\n")
        f.write(f"    TCGv_i64 {self.regV} = tcg_temp_new_i64();\n")
        f.write(f"    tcg_gen_concat_i32_i64({self.regV},\n")
        f.write(f"        hex_gpr[{self.regN}],\n")
        f.write(f"        hex_gpr[{self.regN} + 1]);\n")

class PairReadWrite(ReadWrite):
    def genptr_decl(self, f, tag, regno):
        f.write(f"    const int {self.regN} = insn->regno[{regno}];\n")
        f.write(
            f"    TCGv_i64 {self.regV} = "
            f"get_result_gpr_pair(ctx, {self.regN});\n"
        )
        f.write(f"    tcg_gen_concat_i32_i64({self.regV},\n")
        f.write(f"        hex_gpr[{self.regN}],\n")
        f.write(f"        hex_gpr[{self.regN} + 1]);\n")
    def genptr_dst_write(self, f, tag):
        f.write(f"    gen_log_reg_write_pair(ctx, {self.regN}, {self.regV});\n")

class ControlPairDest(Dest):
    def genptr_decl(self, f, tag, regno):
        f.write(
            f"    const int {self.regN} = "
            f"insn->regno[{regno}] + HEX_REG_SA0;\n"
        )
        f.write(
            f"    TCGv_i64 {self.regV} = "
            f"get_result_gpr_pair(ctx, {self.regN});\n"
        )
    def genptr_dst_write(self, f, tag):
        f.write(
            f"    gen_write_ctrl_reg_pair(ctx, {self.regN}, {self.regV});\n"
        )

class ControlPairSource(Source):
    def genptr_decl(self, f, tag, regno):
        f.write(
            f"    const int {self.regN} = "
            f"insn->regno[{regno}] + HEX_REG_SA0;\n"
        )
        f.write(f"    TCGv_i64 {self.regV} = tcg_temp_new_i64();\n")
        f.write(f"    gen_read_ctrl_reg_pair(ctx, {self.regN}, {self.regV});\n")

class HvxDest(Dest):
    def is_scalar_reg(self):
        return False
    def is_hvx_reg(self):
        return True

class HvxSource(Source):
    def is_scalar_reg(self):
        return False
    def is_hvx_reg(self):
        return True

class HvxReadWrite(ReadWrite):
    def is_scalar_reg(self):
        return False
    def is_hvx_reg(self):
        return True

class VRegDest(HvxDest):
    def genptr_decl(self, f, tag, regno):
        f.write(f"    const int {self.regN} = " f"insn->regno[{regno}];\n")
        f.write(f"    const intptr_t {self.regV}_off =\n")
        if is_tmp_result(tag):
            f.write(f"        ctx_tmp_vreg_off(ctx, {self.regN}, 1, true);\n")
        else:
            f.write(
                f"        ctx_future_vreg_off(ctx, {self.regN}, 1, true);\n"
            )
        if not skip_qemu_helper(tag):
            f.write(f"    TCGv_ptr {self.regV} = " "tcg_temp_new_ptr();\n")
            f.write(
                f"    tcg_gen_addi_ptr({self.regV}, tcg_env, "
                f"{self.regV}_off);\n"
            )
    def genptr_dst_write(self, f, tag):
        pass

class VRegSource(HvxSource):
    def genptr_decl(self, f, tag, regno):
        f.write(f"    const int {self.regN} = insn->regno[{regno}];\n")
        f.write(f"    const intptr_t {self.regV}_off =\n")
        f.write(f"        vreg_src_off(ctx, {self.regN});\n")
        if not skip_qemu_helper(tag):
            f.write(f"    TCGv_ptr {self.regV} = " "tcg_temp_new_ptr();\n")
            f.write(
                f"    tcg_gen_addi_ptr({self.regV}, tcg_env, "
                f"{self.regV}_off);\n"
            )

class VRegNewSource(HvxSource):
    def genptr_decl(self, f, tag, regno):
        f.write(f"    const intptr_t {self.regN}_num = insn->regno[{regno}];\n")
        if skip_qemu_helper(tag):
            f.write(f"    const intptr_t {self.regN}_off =\n")
            f.write(
                f"         ctx_future_vreg_off(ctx, {self.regN}_num, "
                f"1, true);\n"
            )
        else:
            f.write(
                f"    TCGv {self.regN} = tcg_constant_tl({self.regN}_num);\n"
            )

class VRegReadWrite(HvxReadWrite):
    def genptr_decl(self, f, tag, regno):
        f.write(f"    const int {self.regN} = " f"insn->regno[{regno}];\n")
        f.write(f"    const intptr_t {self.regV}_off =\n")
        if is_tmp_result(tag):
            f.write(f"        ctx_tmp_vreg_off(ctx, {self.regN}, 1, true);\n")
        else:
            f.write(
                f"        ctx_future_vreg_off(ctx, {self.regN}, 1, true);\n"
            )
        if not skip_qemu_helper(tag):
            f.write(f"    TCGv_ptr {self.regV} = " "tcg_temp_new_ptr();\n")
            f.write(
                f"    tcg_gen_addi_ptr({self.regV}, tcg_env, "
                f"{self.regV}_off);\n"
            )
        f.write(f"    tcg_gen_gvec_mov(MO_64, {self.regV}_off,\n")
        f.write(f"        vreg_src_off(ctx, {self.regN}),\n")
        f.write("        sizeof(MMVector), sizeof(MMVector));\n")
    def genptr_dst_write(self, f, tag):
        pass

class VRegTmp(HvxReadWrite):
    def genptr_decl(self, f, tag, regno):
        f.write(f"    const int {self.regN} = " f"insn->regno[{regno}];\n")
        f.write(f"    const intptr_t {self.regV}_off =\n")
        f.write("        offsetof(CPUHexagonState, vtmp);\n")
        if not skip_qemu_helper(tag):
            f.write(f"    TCGv_ptr {self.regV} = " "tcg_temp_new_ptr();\n")
            f.write(
                f"    tcg_gen_addi_ptr({self.regV}, tcg_env, "
                f"{self.regV}_off);\n"
            )
        f.write(f"    tcg_gen_gvec_mov(MO_64, {self.regV}_off,\n")
        f.write(f"        vreg_src_off(ctx, {self.regN}),\n")
        f.write(f"        sizeof(MMVector), sizeof(MMVector));\n")
    def genptr_dst_write(self, f, tag):
        f.write(
            f"    gen_log_vreg_write(ctx, {self.regV}_off, {self.regN}, "
            f"{hvx_newv(tag)});\n"
        )

class VRegPairDest(HvxDest):
    def genptr_decl(self, f, tag, regno):
        f.write(f"    const int {self.regN} =  insn->regno[{regno}];\n")
        f.write(f"    const intptr_t {self.regV}_off =\n")
        if is_tmp_result(tag):
            f.write(
                f"        ctx_tmp_vreg_off(ctx, {self.regN}, 2, " "true);\n"
            )
        else:
            f.write(
                f"        ctx_future_vreg_off(ctx, {self.regN}, 2, true);\n"
            )
        if not skip_qemu_helper(tag):
            f.write(f"    TCGv_ptr {self.regV} = tcg_temp_new_ptr();\n")
            f.write(
                f"    tcg_gen_addi_ptr({self.regV}, tcg_env, "
                f"{self.regV}_off);\n"
            )
    def genptr_dst_write(self, f, tag):
        pass

class VRegPairSource(HvxSource):
    def genptr_decl(self, f, tag, regno):
        f.write(f"    const int {self.regN} = insn->regno[{regno}];\n")
        f.write(f"    const intptr_t {self.regV}_off =\n")
        f.write(f"        offsetof(CPUHexagonState, {self.regV});\n")
        if not skip_qemu_helper(tag):
            f.write(f"    TCGv_ptr {self.regV} = " "tcg_temp_new_ptr();\n")
            f.write(
                f"    tcg_gen_addi_ptr({self.regV}, tcg_env, "
                f"{self.regV}_off);\n"
            )
        f.write(f"    tcg_gen_gvec_mov(MO_64, {self.regV}_off,\n")
        f.write(f"        vreg_src_off(ctx, {self.regN}),\n")
        f.write("        sizeof(MMVector), sizeof(MMVector));\n")
        f.write("    tcg_gen_gvec_mov(MO_64,\n")
        f.write(f"        {self.regV}_off + sizeof(MMVector),\n")
        f.write(f"        vreg_src_off(ctx, {self.regN} ^ 1),\n")
        f.write("        sizeof(MMVector), sizeof(MMVector));\n")

class VRegPairReadWrite(HvxReadWrite):
    def genptr_decl(self, f, tag, regno):
        f.write(f"    const int {self.regN} = insn->regno[{regno}];\n")
        f.write(f"    const intptr_t {self.regV}_off =\n")
        f.write(f"        offsetof(CPUHexagonState, {self.regV});\n")
        if not skip_qemu_helper(tag):
            f.write(f"    TCGv_ptr {self.regV} = " "tcg_temp_new_ptr();\n")
            f.write(
                f"    tcg_gen_addi_ptr({self.regV}, tcg_env, "
                f"{self.regV}_off);\n"
            )
        f.write(f"    tcg_gen_gvec_mov(MO_64, {self.regV}_off,\n")
        f.write(f"        vreg_src_off(ctx, {self.regN}),\n")
        f.write("        sizeof(MMVector), sizeof(MMVector));\n")
        f.write("    tcg_gen_gvec_mov(MO_64,\n")
        f.write(f"        {self.regV}_off + sizeof(MMVector),\n")
        f.write(f"        vreg_src_off(ctx, {self.regN} ^ 1),\n")
        f.write("        sizeof(MMVector), sizeof(MMVector));\n")
    def genptr_dst_write(self, f, tag):
        f.write(
            f"    gen_log_vreg_write_pair(ctx, {self.regV}_off, {self.regN}, "
            f"{hvx_newv(tag)});\n"
        )

class QRegDest(HvxDest):
    def genptr_decl(self, f, tag, regno):
        f.write(f"    const int {self.regN} = insn->regno[{regno}];\n")
        f.write(f"    const intptr_t {self.regV}_off =\n")
        f.write(f"        get_result_qreg(ctx, {self.regN});\n")
        if not skip_qemu_helper(tag):
            f.write(f"    TCGv_ptr {self.regV} = " "tcg_temp_new_ptr();\n")
            f.write(
                f"    tcg_gen_addi_ptr({self.regV}, tcg_env, "
                f"{self.regV}_off);\n"
            )
    def genptr_dst_write(self, f, tag):
        pass

class QRegSource(HvxSource):
    def genptr_decl(self, f, tag, regno):
        f.write(f"    const int {self.regN} = insn->regno[{regno}];\n")
        f.write(f"    const intptr_t {self.regV}_off =\n")
        f.write(f"        offsetof(CPUHexagonState, QRegs[{self.regN}]);\n")
        if not skip_qemu_helper(tag):
            f.write(f"    TCGv_ptr {self.regV} = " "tcg_temp_new_ptr();\n")
            f.write(
                f"    tcg_gen_addi_ptr({self.regV}, tcg_env, "
                f"{self.regV}_off);\n"
            )

class QRegReadWrite(HvxReadWrite):
    def genptr_decl(self, f, tag, regno):
        f.write(f"    const int {self.regN} = insn->regno[{regno}];\n")
        f.write(f"    const intptr_t {self.regV}_off =\n")
        f.write(f"        get_result_qreg(ctx, {self.regN});\n")
        if not skip_qemu_helper(tag):
            f.write(f"    TCGv_ptr {self.regV} = " "tcg_temp_new_ptr();\n")
            f.write(
                f"    tcg_gen_addi_ptr({self.regV}, tcg_env, "
                f"{self.regV}_off);\n"
            )
        f.write(f"    tcg_gen_gvec_mov(MO_64, {self.regV}_off,\n")
        f.write(f"        offsetof(CPUHexagonState, QRegs[{self.regN}]),\n")
        f.write("        sizeof(MMQReg), sizeof(MMQReg));\n")
    def genptr_dst_write(self, f, tag):
        pass

def init_registers():
    registers["Rd"] = GprDest("R", "d")
    registers["Re"] = GprDest("R", "e")
    registers["Rs"] = GprSource("R", "s")
    registers["Rt"] = GprSource("R", "t")
    registers["Ru"] = GprSource("R", "u")
    registers["Rv"] = GprSource("R", "v")
    registers["Rx"] = GprReadWrite("R", "x")
    registers["Ry"] = GprReadWrite("R", "y")
    registers["Cd"] = ControlDest("C", "d")
    registers["Cs"] = ControlSource("C", "s")
    registers["Mu"] = ModifierSource("M", "u")
    registers["Pd"] = PredDest("P", "d")
    registers["Pe"] = PredDest("P", "e")
    registers["Ps"] = PredSource("P", "s")
    registers["Pt"] = PredSource("P", "t")
    registers["Pu"] = PredSource("P", "u")
    registers["Pv"] = PredSource("P", "v")
    registers["Px"] = PredReadWrite("P", "x")
    registers["Rdd"] = PairDest("R", "dd")
    registers["Ree"] = PairDest("R", "ee")
    registers["Rss"] = PairSource("R", "ss")
    registers["Rtt"] = PairSource("R", "tt")
    registers["Rxx"] = PairReadWrite("R", "xx")
    registers["Ryy"] = PairReadWrite("R", "yy")
    registers["Cdd"] = ControlPairDest("C", "dd")
    registers["Css"] = ControlPairSource("C", "ss")
    registers["Vd"] = VRegDest("V", "d")
    registers["Vs"] = VRegSource("V", "s")
    registers["Vu"] = VRegSource("V", "u")
    registers["Vv"] = VRegSource("V", "v")
    registers["Vw"] = VRegSource("V", "w")
    registers["Vx"] = VRegReadWrite("V", "x")
    registers["Vy"] = VRegTmp("V", "y")
    registers["Vdd"] = VRegPairDest("V", "dd")
    registers["Vuu"] = VRegPairSource("V", "uu")
    registers["Vvv"] = VRegPairSource("V", "vv")
    registers["Vxx"] = VRegPairReadWrite("V", "xx")
    registers["Qd"] = QRegDest("Q", "d")
    registers["Qe"] = QRegDest("Q", "e")
    registers["Qs"] = QRegSource("Q", "s")
    registers["Qt"] = QRegSource("Q", "t")
    registers["Qu"] = QRegSource("Q", "u")
    registers["Qv"] = QRegSource("Q", "v")
    registers["Qx"] = QRegReadWrite("Q", "x")

    new_registers["Ns"] = GprNewSource("N", "s")
    new_registers["Nt"] = GprNewSource("N", "t")
    new_registers["Pt"] = PredNewSource("P", "t")
    new_registers["Pu"] = PredNewSource("P", "u")
    new_registers["Pv"] = PredNewSource("P", "v")
    new_registers["Os"] = VRegNewSource("O", "s")

def get_register(tag, regtype, regid):
    if is_old_val(regtype, regid, tag):
        return registers[f"{regtype}{regid}"]
    else:
        return new_registers[f"{regtype}{regid}"]
