#!/usr/bin/env python3
#
# Copyright (c) 2024-2025 Michael Clark
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.

import re
import sys
import csv
import glob
import string
import argparse

gpr_bh = ["ah", "ch", "dh", "bh"]
gpr_b = ["al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil"]
gpr_w = ["ax", "cx", "dx", "bx", "sp", "bp", "si", "di"]
gpr_d = ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"]
gpr_q = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi"]
seg_r = ["es", "cs", "ss", "ds", "fs", "gs", "seg6", "seg7"]
sys_r = ["rip", "rflags","fpcsr", "mxcsr"]
sys_n = ["none"]

cc_all = [ 'EQ', 'NEQ', 'GT', 'NLE', 'GE', 'NLT', 'LT', 'NGE', 'LE', 'NGT',
                        'A',  'NBE', 'AE', 'NB',  'B',  'NAE', 'BE', 'NA' ]
cc_signed = [ 'EQ', 'GE', 'GT', 'LE', 'LT', 'NEQ', 'NGT', 'NLE', 'NLT' ]
cc_unsigned = [ 'EQ', 'AE', 'A',  'BE', 'B',  'NEQ', 'NA',  'NBE', 'NB' ]

def gen_range(fmt,f,s,e):
    t = []
    for i in range(s,e):
        t += [[i, fmt % i, f]]
    return t

def gen_list(l,f,start):
    t = []
    for i, s in enumerate(l):
        t += [[i + start, s, f]]
    return t

def gen_sep():
    return [[0, "", ""]]

def reg_table():
    t = []
    t += gen_list(gpr_bh, "reg_bl", 4)
    t += gen_sep()
    t += gen_list(gpr_b, "reg_b", 0)
    t += gen_range("r%db", "reg_b", 8, 32)
    t += gen_sep()
    t += gen_list(gpr_w, "reg_w", 0)
    t += gen_range("r%dw", "reg_w", 8, 32)
    t += gen_sep()
    t += gen_list(gpr_d, "reg_d", 0)
    t += gen_range("r%dd", "reg_d", 8, 32)
    t += gen_sep()
    t += gen_list(gpr_q, "reg_q", 0)
    t += gen_range("r%d", "reg_q", 8, 32)
    t += gen_sep()
    t += gen_range("mm%d", "reg_mmx", 0, 8)
    t += gen_sep()
    t += gen_range("xmm%d", "reg_xmm", 0, 32)
    t += gen_sep()
    t += gen_range("ymm%d", "reg_ymm", 0, 32)
    t += gen_sep()
    t += gen_range("zmm%d", "reg_zmm", 0, 32)
    t += gen_sep()
    t += gen_range("k%d", "reg_kmask", 0, 8)
    t += gen_sep()
    t += gen_range("st(%d)", "reg_fpu", 0, 8)
    t += gen_sep()
    t += gen_range("bnd%d", "reg_bnd", 0, 8)
    t += gen_sep()
    t += gen_range("dr%d", "reg_dreg", 0, 16)
    t += gen_sep()
    t += gen_range("cr%d", "reg_creg", 0, 16)
    t += gen_sep()
    t += gen_list(seg_r, "reg_sreg", 0)
    t += gen_sep()
    t += gen_list(sys_r, "reg_sys", 0)
    t += gen_sep()
    t += gen_list(sys_n, "reg_sys", 31)
    return t

operand_map = {
    '1'                                                     : 'one/r',
    'RAX (r)'                                               : 'rax/r',
    'RAX (r, w)'                                            : 'rax/rw',
    'RAX (w)'                                               : 'rax/w',
    'RCX (r)'                                               : 'rcx/r',
    'RCX (r, w)'                                            : 'rcx/rw',
    'RCX (w)'                                               : 'rcx/w',
    'RDX (r)'                                               : 'rdx/r',
    'RDX (r, w)'                                            : 'rdx/rw',
    'RDX (w)'                                               : 'rdx/w',
    'RBX (r)'                                               : 'rbx/r',
    'RBX (r, w)'                                            : 'rbx/rw',
    'RBX (w)'                                               : 'rbx/w',
    'RSI (r)'                                               : 'rsi/r',
    'RSI (r, w)'                                            : 'rsi/rw',
    'RSI (w)'                                               : 'rsi/w',
    'RDI (r)'                                               : 'rdi/r',
    'RDI (r, w)'                                            : 'rdi/rw',
    'RDI (w)'                                               : 'rdi/w',
    'ST0 (r)'                                               : 'st0/r',
    'ST0 (r, w)'                                            : 'st0/rw',
    'ST0 (w)'                                               : 'st0/w',
    'STX (r)'                                               : 'stx/r',
    'STX (r, w)'                                            : 'stx/rw',
    'STX (w)'                                               : 'stx/w',
    'SEG (r)'                                               : 'seg/r',
    'SEG (r, w)'                                            : 'seg/rw',
    'SEG (w)'                                               : 'seg/w',
    'RSP (r, w, i)'                                         : 'rsp/rwi',
    'RBP (r, w, i)'                                         : 'rbp/rwi',
    'MXCSR (r, i)'                                          : 'mxcsr/ri',
    'MXCSR (w, i)'                                          : 'mxcsr/wi',
    'RFLAGS (r, i)'                                         : 'rflags/ri',
    'RFLAGS (w, i)'                                         : 'rflags/wi',
    'ModRM:reg (r)'                                         : 'reg/r',
    'ModRM:reg (r, w)'                                      : 'reg/rw',
    'ModRM:reg (w)'                                         : 'reg/w',
    'ModRM:r/m (r)'                                         : 'mrm/r',
    'ModRM:r/m (r, w)'                                      : 'mrm/rw',
    'ModRM:r/m (w)'                                         : 'mrm/w',
    'ModRM:r/m (r, ModRM:[7:6] must be 11b)'                : 'mrm/r',
    'ModRM:r/m (r, ModRM:[7:6] must not be 11b)'            : 'mrm/r',
    'ModRM:r/m (w, ModRM:[7:6] must not be 11b)'            : 'mrm/w',
    'ModRM:r/m (r, w, ModRM:[7:6] must not be 11b)'         : 'mrm/rw',
    'BaseReg (r): VSIB:base, VectorReg (r): VSIB:index'     : 'sib/r',
    'SIB.base (r): Address of pointer SIB.index (r)'        : 'sib/r',
    'EVEX.vvvv (r)'                                         : 'vec/r',
    'EVEX.vvvv (w)'                                         : 'vec/w',
    'VEX.vvvv (r)'                                          : 'vec/r',
    'VEX.vvvv (r, w)'                                       : 'vec/rw',
    'VEX.vvvv (w)'                                          : 'vec/w',
    'ib'                                                    : 'imm',
    'iw'                                                    : 'imm',
    'iwd'                                                   : 'imm',
    'i16'                                                   : 'imm',
    'i32'                                                   : 'imm',
    'i64'                                                   : 'imm',
    'imm'                                                   : 'imm',
    'ime'                                                   : 'ime',
    'ib[3:0]'                                               : 'imm',
    'ib[7:4]'                                               : 'is4/r',
    'Implicit XMM0 (r)'                                     : 'xmm0/r',
    'Implicit XMM0-7 (r, w)'                                : 'xmm0_7/rw',
    'opcode +r (r)'                                         : 'opr/r',
    'opcode +r (r, w)'                                      : 'opr/rw',
    'opcode +r (w)'                                         : 'opr/w',
    'NA'                                                    : None,
    ''                                                      : None
}

opcode_map = {
    '<xmm0>'  : 'reg_xmm0',
    '<xmm0-7>': 'reg_xmm0_7',
    '1'       : '1',
    'm'       : 'mem',
    'al'      : 'reg_al',
    'cl'      : 'reg_cl',
    'ah'      : 'reg_ah',
    'aw'      : 'reg_aw',
    'cw'      : 'reg_cw',
    'dw'      : 'reg_dw',
    'bw'      : 'reg_bw',
    'ax'      : 'reg_ax',
    'cx'      : 'reg_cx',
    'dx'      : 'reg_dx',
    'bx'      : 'reg_bx',
    'eax'     : 'reg_eax',
    'ecx'     : 'reg_ecx',
    'edx'     : 'reg_edx',
    'ebx'     : 'reg_ebx',
    'rax'     : 'reg_rax',
    'rcx'     : 'reg_rcx',
    'rdx'     : 'reg_rdx',
    'rbx'     : 'reg_rbx',
    'si'      : 'reg_si',
    'di'      : 'reg_di',
    'pa'      : 'reg_pa',
    'pc'      : 'reg_pc',
    'pd'      : 'reg_pd',
    'pb'      : 'reg_pb',
    'psi'     : 'reg_psi',
    'pdi'     : 'reg_pdi',
    'cs'      : 'seg_cs',
    'ds'      : 'seg_ds',
    'ss'      : 'seg_ss',
    'es'      : 'seg_es',
    'fs'      : 'seg_fs',
    'gs'      : 'seg_gs',
    'sreg'    : 'seg',
    'dr0-dr7' : 'dreg',
    'cr0-cr15': 'creg',
    'cr8'     : 'creg8',
    'st(0)'   : 'reg_st0',
    'st(1)'   : 'reg_st1',
    'st(i)'   : 'st'
}

def x86_mode(row):
    l = list()
    if row['Valid 64-bit'] == 'Valid':
        l.append('64')
    if row['Valid 32-bit'] == 'Valid':
        l.append('32')
    if row['Valid 16-bit'] == 'Valid':
        l.append('16')
    return "/".join(l)

def x86_operand(opcode,row):
    l = list()
    opcode = opcode.split(' ')[0]
    operand1 = operand_map[row['Operand 1']]
    operand2 = operand_map[row['Operand 2']]
    operand3 = operand_map[row['Operand 3']]
    operand4 = operand_map[row['Operand 4']]
    if operand1:
        l += [operand1]
    if operand2:
        l += [operand2]
    if operand3:
        l += [operand3]
    if operand4:
        l += [operand4]
    return ",".join(l)

def cleanup_oprs(args):
    args = list(map(lambda x : x.lstrip().rstrip(), args.split(",")))
    args = list(map(lambda x : x.replace('&',':'), args))
    args = list(map(lambda x : x.replace('{k1}','{k}'), args))
    args = list(map(lambda x : x.lower(), args))
    args = list(map(lambda x : opcode_map[x] if x in opcode_map else x, args))
    for reg in ('r32', 'r64'):
        for suffix in ('a', 'b'):
            args = list(map(lambda x : x.replace(reg + suffix, reg), args))
    args = list(map(lambda x : 'rw' if x == 'r' else x, args))
    args = list(map(lambda x : 'rw/mw' if x == 'r/m' else x, args))
    for reg in ('k', 'bnd', 'mm', 'xmm', 'ymm', 'zmm'):
        for i in range(0,5):
            args = list(map(lambda x : x.replace(reg + str(i), reg) \
                if x.find(reg) == 0 else x, args))
    args = list(map(lambda x : x.replace(' ', ''), args))
    return args

def split_opcode(opcode):
    space_idx = opcode.find(' ')
    if space_idx == -1:
        return (opcode,list())
    else:
        return (opcode[:space_idx], cleanup_oprs(opcode[space_idx:]))

def cleanup_opcode(opcode):
    op, args = split_opcode(opcode)
    if len(args) > 0:
        return "%s %s " % (op, ",".join(args))
    else:
        return op

def cleanup_encoding(enc):
    enc = enc.lower()
    enc = enc.replace('/is4', 'ib')
    enc = enc.replace('0f 38', '0f38')
    enc = enc.replace('0f 3a', '0f3a')
    enc = enc.replace('  ', ' ')
    return enc

def translate_modes(modes):
    modelist = []
    if modes == '':
        return '0'
    for m in modes.split('/'):
        modelist += ['x86_modes_%s' % m]
    return "|".join(modelist)

# add 9b, del rex rex.w
def translate_encoding(enc):
    prefixes = [ 'hex', 'lex', 'vex', 'evex' ]
    r_suffixes = [ 'rep', 'lock', 'norexb' ]
    s_suffixes = [ 'o16', 'o32', 'o64', 'a16', 'a32', 'a64' ]
    pbytes = [ '66', '9b', 'f2', 'f3' ]
    maps = { '0f', '0f38', '0f3a', 'map4', 'map5', 'map6' }
    widths = { 'w0', 'w1', 'wig', 'wb', 'wn', 'ws', 'wx', 'ww' }
    lengths = { 'lig', 'lz', 'l0', 'l1', '128', '256', '512' }
    flags = { 'nds', 'ndd', 'dds' }
    imm = { 'ib', 'iw', 'iwd', 'i16', 'i32', 'i64' }
    mods = { '/r', '/0', '/1', '/2', '/3', '/4', '/5', '/6', '/7' }
    pl = []
    opc = ['0x00','0x00']
    opm = ['0x00','0x00']
    oplen = 0
    has_imm, has_pfx, has_pbyte, has_map = False, False, False, False
    comps = enc.split(" ")
    for el in comps:
        is_hex = all(c in string.hexdigits for c in el[0:2])
        p = None
        for sel in prefixes:
            if el.find(sel) == 0 and ( p == None or len(sel) > len(p) ):
                p = sel
        if p:
            pl += ['x86_enc_t_%s' % p.replace('.', '_')]
            el = el[len(p):]
            vp, vm, vw, vl, vf = None, None, None, None, None
            for sel in el.split('.'):
                if sel == '':
                    pass
                elif sel in pbytes:
                    vp = 'x86_enc_p_%s' % sel
                elif sel in maps:
                    vm = 'x86_enc_m_%s' % sel
                elif sel in widths:
                    vw = 'x86_enc_w_%s' % sel
                elif sel in lengths:
                    vl = 'x86_enc_l_%s' % sel
                elif sel in flags:
                    vf = 'x86_enc_f_%s' % sel
                else:
                    raise Exception("unknown element '%s' for encoding"
                        " '%s" % (sel, enc))
            if vp:
                pl += [vp]
            if vm:
                pl += [vm]
            if vw:
                pl += [vw]
            if vl:
                pl += [vl]
            if vf:
                pl += [vf]
            if p == 'vex' or p == 'evex' or p == 'lex':
                has_pfx = True
        elif el in maps and len(comps) > 1 and not (has_map or has_pfx):
            pl += ['x86_enc_m_%s' % el]
            has_map = True
        elif el in r_suffixes:
            pl += ['x86_enc_r_%s' % el]
        elif el in s_suffixes:
            pl += ['x86_enc_s_%s' % el]
        elif el in imm:
            if has_imm:
                # additional immediate used by CALLF/JMPF/ENTER
                if el in { 'ib', 'i16' }:
                    pl += ['x86_enc_j_%s' % el]
                else:
                    raise Exception("illegal immediate '%s' for encoding"
                        " '%s" % (el, enc))
            else:
                pl += ['x86_enc_i_%s' % el]
                has_imm = True
        elif el in mods:
            if oplen == 2:
                raise Exception("opcode '%s' limit exceeded for encoding"
                    " '%s" % (el, enc))
            pl += ['x86_enc_f_modrm_r' if el == '/r' else 'x86_enc_f_modrm_n']
            if el != '/r':
                opc[oplen] = '0x{:02x}'.format(int(el[1]) << 3)
                opm[oplen] = '0x38'
            oplen += 1
        elif len(el) == 2 and is_hex:
            if oplen == 2:
                raise Exception("opcode '%s' limit exceeded for encoding"
                    " '%s" % (el, enc))
            if oplen == 1:
                pl += ['x86_enc_f_opcode']
            opc[oplen] = '0x%s' % el[0:2]
            opm[oplen] = '0xff'
            oplen += 1
        elif len(el) == 4 and is_hex and el[2:4] == '+r':
            if oplen == 2:
                raise Exception("opcode '%s' limit exceeded for encoding "
                    "'%s" % (el, enc))
            pl += ['x86_enc_%s_opcode_r' % ('o' if oplen == 0 else 'f')]
            opc[oplen] = '0x%s' % el[0:2]
            opm[oplen] = '0xf8'
            oplen += 1
        else:
            raise Exception("unknown element '%s' for encoding "
                "'%s" % (el, enc))
    return "|".join(pl), opc, opm

def translate_operands(operands):
    oprlist = []
    typpat = re.compile('([if])(\\d+)x(\\d+)')
    for i,arg0 in enumerate(operands):
        flags = arg0.split('{')
        argcomps = []
        for j,arg1 in enumerate(flags):
            cp = arg1.find('}')
            if cp == -1:
                arg1 = arg1.replace(':','_')
                argp = []
                for arg2 in arg1.split('/'):
                    m = typpat.match(arg2)
                    if m:
                        argcomps += ['x86_opr_' + arg2]
                    else:
                        argp += [arg2]
                argcomps += ['x86_opr_' + '_'.join(argp)]
            else:
                arg1 = arg1.replace('}','')
                argcomps += ['x86_opr_flag_' + arg1]
        oprlist += ["|".join(argcomps)]
    return oprlist

def translate_order(order):
    ol = []
    if order:
        for o in order.split(','):
            o = o.replace(':','_')
            ol.append("|".join(map(lambda x: 'x86_ord_' + x, o.split('/'))))
    return ol

def print_insn(x86_insn):
    for row in x86_insn:
        opcode, enc, modes, ext, order, tt, desc = row
        opcode = opcode.replace('reg_','')
        print("| %-53s | %-31s | %-23s | %-8s |" % \
            (opcode, enc, order, modes))

def opcode_list(x86_insn):
    ops = set()
    for row in x86_insn:
        opcode, enc, modes, ext, order, tt, desc = row
        op, opr = split_opcode(opcode)
        ops.add(op)
    return ['NIL'] + sorted(ops)

def operand_list(x86_insn):
    oprset = set()
    for idx, row in enumerate(x86_insn):
        opcode, enc, modes, ext, order, tt, desc = row
        op, opr = split_opcode(opcode)
        oprset.add(tuple(translate_operands(opr)))
    return sorted(oprset)

def order_list(x86_insn):
    ordset = set()
    for idx, row in enumerate(x86_insn):
        opcode, enc, modes, ext, order, tt, desc = row
        ordset.add(tuple(translate_order(order)))
    return sorted(ordset)

opcode_enums_template = """/* generated source */
enum x86_reg\n{%s};
enum x86_op\n{%s};"""

opcode_table_template = """/* generated source */
const size_t x86_opc_table_size = %d;
const size_t x86_opr_table_size = %d;
const size_t x86_ord_table_size = %d;
const size_t x86_op_names_size = %d;
const x86_opc_data x86_opc_table[] =\n{
  { x86_op_NIL, 0, 0, 0, 0, { { 0, 0 } }, { { 0, 0 } } },%s};
const x86_opr_data x86_opr_table[] =\n{%s};
const x86_ord_data x86_ord_table[] =\n{%s};
const char* x86_op_names[] =\n{%s};
const char* x86_reg_names[512] =\n{%s};"""

def print_opcode_enums(x86_reg, x86_insn):
    regstr, opstr = '\n', '\n'
    for i,s,f in x86_reg:
        n = s.replace('(','').replace(')','')
        regstr += '\n' if len(s) == 0 else \
            '  %-10s = %s,\n' % ('x86_%s' % n, 'x86_%s | %d' % (f, i))
    for op in opcode_list(x86_insn):
        opstr += '    x86_op_%s,\n' % op
    print(opcode_enums_template % (regstr, opstr))

def print_opcode_tables(x86_reg, x86_insn):
    oplist = opcode_list(x86_insn)
    oprlist = operand_list(x86_insn)
    ordlist = order_list(x86_insn)
    oprmap = {v: i for i, v in enumerate(oprlist)}
    ordmap = {v: i for i, v in enumerate(ordlist)}
    opcstr, oprstr, ordstr, opsstr, regstr = '\n', '\n', '\n', '\n', '\n'
    for idx, row in enumerate(x86_insn):
        opcode, enc, modes, ext, order, tt, desc = row
        op, opr = split_opcode(opcode)
        oprl = translate_operands(opr)
        ordl = translate_order(order)
        oprc = oprmap[tuple(oprl)]
        ordc = ordmap[tuple(ordl)]
        modes = translate_modes(modes)
        enc, opc, opm = translate_encoding(enc)
        opcstr += '  { %s, %s, %d, %d, %s, { %s }, { %s } },\n' % \
            ('x86_op_%s' % op, modes, oprc, ordc, enc,
                '{ %s, %s }' % (opc[0], opc[1]),
                '{ %s, %s }' % (opm[0], opm[1]))
    for x in oprlist:
        oprstr += '  { { %s } },\n' % (", ".join(['0'] if not x else x))
    for x in ordlist:
        ordstr += '  { { %s } },\n' % (", ".join(['0'] if not x else x))
    for op in oplist:
        opsstr += '    "' + op.lower() + '",\n'
    for i,s,f in x86_reg:
        n = s.replace('(','').replace(')','')
        regstr += '\n' if len(s) == 0 else \
                  '    %-12s = \"%s\",\n' % ('[x86_%s]' % n, s)
    print(opcode_table_template % (
        len(x86_insn) + 1, len(oprlist), len(ordlist), len(oplist),
        opcstr, oprstr, ordstr, opsstr, regstr)
    )

def read_data(files):
    data = []
    if not isinstance(files, list):
        files = glob.glob(files)
    for csvpath in files:
        file = open(csvpath, encoding='utf-8-sig', newline='')
        reader = csv.DictReader(file, delimiter=',', quotechar='"')
        for row in reader:
            data += [row]
    data.sort(key=lambda x: (
            x['Instruction'].split(' ')[0],
            x['Opcode'].split(' ')[1],
            x['Instruction']))
    insn = []
    for row in data:
        opcode = cleanup_opcode(row['Instruction'])
        enc = cleanup_encoding(row['Opcode'])
        modes = x86_mode(row)
        ext = row['Feature Flags']
        order = x86_operand(opcode,row)
        tt = row['Tuple Type']
        desc = row['Description']
        insn += [[opcode, enc, modes, ext, order, tt, desc]]
    return insn

def parse_table(rows):
    rows = [row.strip() for row in rows]
    data = []
    obj = []
    h1 = None
    begun = False
    for row in rows:
        if row.startswith("#"):
            space = row.index(' ')
            hashes = row[:space]
            heading = row[space+1:]
            depth = hashes.count('#')
            if not h1:
                h1 = heading
            obj.append(TableSection(heading, depth))
        elif row.startswith("|"):
            cells = row.split('|')
            cells = [cell.strip() for cell in cells if cell.strip()]
            if cells[0].startswith("-") or cells[0].startswith(":"):
                begun = True
                obj.append(TableHeader())
            elif begun:
                data.append(cells)
                obj.append(TableData(cells))
        else:
            begun = False
            obj.append(TableText(row))
    return { 'title': h1, 'data': data, 'obj': obj }

def read_file(file_path):
    try:
        with open(file_path, 'r') as file:
            lines = file.readlines()
            lines = [line.strip() for line in lines]
        return lines
    except FileNotFoundError:
        print(f"File not found: {file_path}")
        return []

def make_map(x86_insn):
    insn_map = dict()
    for row in x86_insn:
        opcode, enc, modes, ext, order, tt, desc = row
        op, opr = split_opcode(opcode)
        if op not in insn_map:
            insn_map[op] = list()
        insn_map[op].append(row)
    return insn_map

#
# description table types
#
class TableText():
    def __init__(self,text):
        self.text = text
class TableSection():
    def __init__(self,heading,depth):
        self.heading = heading
        self.depth = depth
class TableHeader():
    def __init__(self):
        return
class TableData():
    def __init__(self,cells):
        self.cells = cells

#
# print descriptions with instructions
#
def table_text_insn(self,x86_desc):
    return ""
def table_section_insn(self,x86_desc):
    return "%s %s\n\n" % ("#" * self.depth, self.heading)
def table_header_insn(self,x86_desc):
    return ""
def table_data_insn(self,x86_desc):
    insn_map = x86_desc['insn']
    insn, desc = self.cells
    text = ""
    insn_list = []
    o = insn.find("cc")
    if insn.startswith("v"):
        insn_list.append(insn[1:])
        insn_list.append(insn.upper())
    elif o >= 0:
        for cc in cc_all:
            new_insn = '%s%s%s' % (insn[0:o], cc, insn[o+2:])
            insn_list.append(new_insn)
    else:
        insn_list.append(insn)
    if len(insn_list) > 0:
        text += "\n"
        text += "| %-51s | %-29s | %-23s | %-8s |\n" % \
            ("opcode", "encoding", "order", "modes")
        text += "|:%-51s-|:%-29s-|:%-23s-|:%-8s-|\n" % \
            ("-"*51, "-"*29, "-"*23, "-"*8)
        for insn_name in insn_list:
            if insn_name in insn_map:
                for row in insn_map[insn_name]:
                    opcode, enc, modes, ext, order, tt, desc = row
                    opcode = opcode.replace('reg_','')
                    text += "| %-51s | %-29s | %-23s | %-8s |\n" % \
                        (opcode, enc, order, modes)
        text += "|:%-51s-|:%-29s-|:%-23s-|:%-8s-|\n" % \
            ("-"*51, "-"*29, "-"*23, "-"*8)
    text += "\n\n"
    return "%s %s\n%s" % ("[%s]" % insn, "# %s" % desc, text)

table_insn = {
    TableText: table_text_insn, TableSection: table_section_insn,
    TableHeader: table_header_insn, TableData: table_data_insn,
}

def print_fancy_insn(x86_desc):
    for obj in x86_desc['tab']['obj']:
        print(table_insn[type(obj)](obj, x86_desc), end="")

parser = argparse.ArgumentParser(description='x86 table generator')
parser.add_argument('files',
                    default='data/*.csv', nargs='*',
                    help='x86 csv metadata')
parser.add_argument('--print-insn',
                    default=False, action='store_true',
                    help='print instructions')
parser.add_argument('--print-fancy-insn',
                    default=False, action='store_true',
                    help='print fancy instructions')
parser.add_argument('--print-opcode-enums',
                    default=False, action='store_true',
                    help='print register enum')
parser.add_argument('--print-opcode-tables',
                    default=False, action='store_true',
                    help='print register strings')
parser.add_argument('--output-file', type=argparse.FileType('w'),
                    help="filename to write output to")
args = parser.parse_args()

x86_reg = reg_table()
x86_insn = read_data(args.files)

if args.output_file:
    sys.stdout = args.output_file
if args.print_insn:
    print_insn(x86_insn)
if args.print_opcode_enums:
    print_opcode_enums(x86_reg, x86_insn)
if args.print_opcode_tables:
    print_opcode_tables(x86_reg, x86_insn)
