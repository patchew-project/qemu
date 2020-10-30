##
##  Copyright(c) 2020 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

hexagon_ss = ss.source_set()

prog_python = import('python').find_installation('python3')

hex_common_py = 'hex_common.py'
attribs_def_h = meson.current_source_dir() / 'attribs_def.h'
gen_tcg_h = meson.current_source_dir() / 'gen_tcg.h'

#
#  Step 1
#  We use a C program to create semantics_generated.pyinc
#
gen_semantics = executable('gen_semantics', 'gen_semantics.c')

semantics = custom_target(
    'semantics_generated.pyinc',
    output: 'semantics_generated.pyinc',
    input: gen_semantics,
    command: ['@INPUT@', '@OUTPUT@'],
)
hexagon_ss.add(semantics)

#
# Step 2
# We use Python scripts to generate the following files
#     shortcode_generated.h
#     helper_protos_generated.h
#     tcg_funcs_generated.h
#     tcg_func_table_generated.h
#     helper_funcs_generated.h
#     printinsn_generated.h
#     op_regs_generated.h
#     op_attribs_generated.h
#     opcodes_def_generated.h
#
shortcode_h = custom_target(
    'shortcode_generated.h',
    output: 'shortcode_generated.h',
    input: 'gen_shortcode.py',
    depend_files: [hex_common_py],
    command: [prog_python, '@INPUT@', semantics, attribs_def_h, '@OUTPUT@'],
)
hexagon_ss.add(shortcode_h)

helper_protos_h = custom_target(
    'helper_protos_generated.h',
    output: 'helper_protos_generated.h',
    input: 'gen_helper_protos.py',
    depend_files: [hex_common_py],
    command: [prog_python, '@INPUT@', semantics, attribs_def_h, gen_tcg_h, '@OUTPUT@'],
)
hexagon_ss.add(helper_protos_h)

tcg_funcs_h = custom_target(
    'tcg_funcs_generated.h',
    output: 'tcg_funcs_generated.h',
    input: 'gen_tcg_funcs.py',
    depend_files: [hex_common_py],
    command: [prog_python, '@INPUT@', semantics, attribs_def_h, gen_tcg_h, '@OUTPUT@'],
)
hexagon_ss.add(tcg_funcs_h)

tcg_func_table_h = custom_target(
    'tcg_func_table_generated.h',
    output: 'tcg_func_table_generated.h',
    input: 'gen_tcg_func_table.py',
    depend_files: [hex_common_py],
    command: [prog_python, '@INPUT@', semantics, attribs_def_h, '@OUTPUT@'],
)
hexagon_ss.add(tcg_func_table_h)

helper_funcs_h = custom_target(
    'helper_funcs_generated.h',
    output: 'helper_funcs_generated.h',
    input: 'gen_helper_funcs.py',
    depend_files: [hex_common_py],
    command: [prog_python, '@INPUT@', semantics, attribs_def_h, gen_tcg_h, '@OUTPUT@'],
)
hexagon_ss.add(helper_funcs_h)

printinsn_h = custom_target(
    'printinsn_generated.h',
    output: 'printinsn_generated.h',
    input: 'gen_printinsn.py',
    depend_files: [hex_common_py],
    command: [prog_python, '@INPUT@', semantics, attribs_def_h, '@OUTPUT@'],
)
hexagon_ss.add(printinsn_h)

op_regs_h = custom_target(
    'op_regs_generated.h',
    output: 'op_regs_generated.h',
    input: 'gen_op_regs.py',
    depend_files: [hex_common_py],
    command: [prog_python, '@INPUT@', semantics, attribs_def_h, '@OUTPUT@'],
)
hexagon_ss.add(op_regs_h)

op_attribs_h = custom_target(
    'op_attribs_generated.h',
    output: 'op_attribs_generated.h',
    input: 'gen_op_attribs.py',
    depend_files: [hex_common_py],
    command: [prog_python, '@INPUT@', semantics, attribs_def_h, '@OUTPUT@'],
)
hexagon_ss.add(op_attribs_h)

opcodes_def_h = custom_target(
    'opcodes_def_generated.h',
    output: 'opcodes_def_generated.h',
    input: 'gen_opcodes_def.py',
    depend_files: [hex_common_py],
    command: [prog_python, '@INPUT@', semantics, attribs_def_h, '@OUTPUT@'],
)
hexagon_ss.add(opcodes_def_h)

#
# Step 3
# We use a C program to create iset.py which is imported into dectree.py
# to create the decode tree
#
gen_dectree_import = executable('gen_dectree_import', 'gen_dectree_import.c', opcodes_def_h, op_regs_h)

iset_py = custom_target(
    'iset.py',
    output: 'iset.py',
    input: gen_dectree_import,
    command: ['@INPUT@', '@OUTPUT@'],
)
hexagon_ss.add(iset_py)

#
# Step 4
# We use the dectree.py script to generate the decode tree header file
#
dectree_h = custom_target(
    'dectree_generated.h',
    output: 'dectree_generated.h',
    input: 'dectree.py',
    depends: [iset_py],
    command: ['PYTHONPATH=' + meson.current_build_dir(), '@INPUT@', '@OUTPUT@'],
)
hexagon_ss.add(dectree_h)

hexagon_ss.add(files(
    'cpu.c',
    'translate.c',
    'op_helper.c',
    'gdbstub.c',
    'genptr.c',
    'reg_fields.c',
    'decode.c',
    'iclass.c',
    'opcodes.c',
    'printinsn.c',
    'arch.c',
    'fma_emu.c',
    'conv_emu.c',
))

target_arch += {'hexagon': hexagon_ss}
