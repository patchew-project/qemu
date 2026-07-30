##
##  Copyright(c) 2020-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

hex_common_py = 'hex_common.py'
gen_tcg_h = meson.current_source_dir() / 'gen_tcg.h'
gen_tcg_hvx_h = meson.current_source_dir() / 'gen_tcg_hvx.h'
gen_tcg_sys_h = meson.current_source_dir() / 'gen_tcg_sys.h'
idef_parser_dir = meson.current_source_dir() / 'idef-parser'

#
#  Step 1
#  We use a C program to create semantics_generated.pyinc
#
gen_semantics = executable(
    'gen_semantics',
    'gen_semantics.c',
    native: true, build_by_default: false)

semantics_generated = custom_target(
    'semantics_generated.pyinc',
    output: 'semantics_generated.pyinc',
    command: [gen_semantics, '@OUTPUT@'],
)
hexagon_ss.add(semantics_generated)

#
# Step 2
# We use Python scripts to generate the following files
#     printinsn_generated.h.inc
#     op_attribs_generated.h.inc
#     opcodes_def_generated.h.inc
#
printinsn_generated = custom_target(
    'printinsn_generated.h.inc',
    output: 'printinsn_generated.h.inc',
    depends: [semantics_generated],
    depend_files: [hex_common_py],
    command: [python, files('gen_printinsn.py'), semantics_generated, '@OUTPUT@'],
)
hexagon_ss.add(printinsn_generated)

op_attribs_generated = custom_target(
    'op_attribs_generated.h.inc',
    output: 'op_attribs_generated.h.inc',
    depends: [semantics_generated],
    depend_files: [hex_common_py],
    command: [python, files('gen_op_attribs.py'), semantics_generated, '@OUTPUT@'],
)
hexagon_ss.add(op_attribs_generated)

opcodes_def_generated = custom_target(
    'opcodes_def_generated.h.inc',
    output: 'opcodes_def_generated.h.inc',
    depends: [semantics_generated],
    depend_files: [hex_common_py],
    command: [python, files('gen_opcodes_def.py'), semantics_generated, '@OUTPUT@'],
)
hexagon_ss.add(opcodes_def_generated)

#
# Step 3
# We use a C program to create iset.py which is imported into dectree.py
# to create the decode tree
#
gen_dectree_import = executable(
    'gen_dectree_import',
    'gen_dectree_import.c', opcodes_def_generated,
    native: true, build_by_default: false)

iset_py = custom_target(
    'iset.py',
    output: 'iset.py',
    command: [gen_dectree_import, '@OUTPUT@'],
)
hexagon_ss.add(iset_py)

#
# Step 4
# Generate the input to the QEMU decodetree.py script
#
normal_decode_generated = custom_target(
    'normal_decode_generated',
    output: 'normal_decode_generated',
    depends: [iset_py, semantics_generated],
    env: {'PYTHONPATH': meson.current_build_dir()},
    command: [python, files('gen_decodetree.py'), semantics_generated, 'NORMAL', '@OUTPUT@'],
)
hexagon_ss.add(normal_decode_generated)

hvx_decode_generated = custom_target(
    'hvx_decode_generated',
    output: 'hvx_decode_generated',
    depends: [iset_py, semantics_generated],
    env: {'PYTHONPATH': meson.current_build_dir()},
    command: [python, files('gen_decodetree.py'), semantics_generated, 'EXT_mmvec', '@OUTPUT@'],
)
hexagon_ss.add(hvx_decode_generated)

subinsn_a_decode_generated = custom_target(
    'subinsn_a_decode_generated',
    output: 'subinsn_a_decode_generated',
    depends: [iset_py, semantics_generated],
    env: {'PYTHONPATH': meson.current_build_dir()},
    command: [python, files('gen_decodetree.py'), semantics_generated, 'SUBINSN_A', '@OUTPUT@'],
)
hexagon_ss.add(subinsn_a_decode_generated)

subinsn_l1_decode_generated = custom_target(
    'subinsn_l1_decode_generated',
    output: 'subinsn_l1_decode_generated',
    depends: [iset_py, semantics_generated],
    env: {'PYTHONPATH': meson.current_build_dir()},
    command: [python, files('gen_decodetree.py'), semantics_generated, 'SUBINSN_L1', '@OUTPUT@'],
)
hexagon_ss.add(subinsn_l1_decode_generated)

subinsn_l2_decode_generated = custom_target(
    'subinsn_l2_decode_generated',
    output: 'subinsn_l2_decode_generated',
    depends: [iset_py, semantics_generated],
    env: {'PYTHONPATH': meson.current_build_dir()},
    command: [python, files('gen_decodetree.py'), semantics_generated, 'SUBINSN_L2', '@OUTPUT@'],
)
hexagon_ss.add(subinsn_l2_decode_generated)

subinsn_s1_decode_generated = custom_target(
    'subinsn_s1_decode_generated',
    output: 'subinsn_s1_decode_generated',
    depends: [iset_py, semantics_generated],
    env: {'PYTHONPATH': meson.current_build_dir()},
    command: [python, files('gen_decodetree.py'), semantics_generated, 'SUBINSN_S1', '@OUTPUT@'],
)
hexagon_ss.add(subinsn_s1_decode_generated)

subinsn_s2_decode_generated = custom_target(
    'subinsn_s2_decode_generated',
    output: 'subinsn_s2_decode_generated',
    depends: [iset_py, semantics_generated],
    env: {'PYTHONPATH': meson.current_build_dir()},
    command: [python, files('gen_decodetree.py'), semantics_generated, 'SUBINSN_S2', '@OUTPUT@'],
)
hexagon_ss.add(subinsn_s2_decode_generated)

#
# Run the QEMU decodetree.py script to produce the instruction decoder
#
decodetree_py = meson.current_source_dir() / '../../scripts/decodetree.py'
decode_normal_generated = custom_target(
    'decode_normal_generated.c.inc',
    output: 'decode_normal_generated.c.inc',
    input: normal_decode_generated,
    env: {'PYTHONPATH': meson.current_build_dir()},
    command: [python, files(decodetree_py), normal_decode_generated, '--static-decode=decode_normal', '-o', '@OUTPUT@'],
)
hexagon_ss.add(decode_normal_generated)

decode_hvx_generated = custom_target(
    'decode_hvx_generated.c.inc',
    output: 'decode_hvx_generated.c.inc',
    input: hvx_decode_generated,
    env: {'PYTHONPATH': meson.current_build_dir()},
    command: [python, files(decodetree_py), hvx_decode_generated, '--static-decode=decode_hvx', '-o', '@OUTPUT@'],
)
hexagon_ss.add(decode_hvx_generated)

decode_subinsn_a_generated = custom_target(
    'decode_subinsn_a_generated.c.inc',
    output: 'decode_subinsn_a_generated.c.inc',
    input: subinsn_a_decode_generated,
    env: {'PYTHONPATH': meson.current_build_dir()},
    command: [python, files(decodetree_py), subinsn_a_decode_generated, ['--static-decode=decode_subinsn_a', '--insnwidth=16'], '-o', '@OUTPUT@'],
)
hexagon_ss.add(decode_subinsn_a_generated)

decode_subinsn_l1_generated = custom_target(
    'decode_subinsn_l1_generated.c.inc',
    output: 'decode_subinsn_l1_generated.c.inc',
    input: subinsn_l1_decode_generated,
    env: {'PYTHONPATH': meson.current_build_dir()},
    command: [python, files(decodetree_py), subinsn_l1_decode_generated, ['--static-decode=decode_subinsn_l1', '--insnwidth=16'], '-o', '@OUTPUT@'],
)
hexagon_ss.add(decode_subinsn_l1_generated)

decode_subinsn_l2_generated = custom_target(
    'decode_subinsn_l2_generated.c.inc',
    output: 'decode_subinsn_l2_generated.c.inc',
    input: subinsn_l2_decode_generated,
    env: {'PYTHONPATH': meson.current_build_dir()},
    command: [python, files(decodetree_py), subinsn_l2_decode_generated, ['--static-decode=decode_subinsn_l2', '--insnwidth=16'], '-o', '@OUTPUT@'],
)
hexagon_ss.add(decode_subinsn_l2_generated)

decode_subinsn_s1_generated = custom_target(
    'decode_subinsn_s1_generated.c.inc',
    output: 'decode_subinsn_s1_generated.c.inc',
    input: subinsn_s1_decode_generated,
    env: {'PYTHONPATH': meson.current_build_dir()},
    command: [python, files(decodetree_py), subinsn_s1_decode_generated, ['--static-decode=decode_subinsn_s1', '--insnwidth=16'], '-o', '@OUTPUT@'],
)
hexagon_ss.add(decode_subinsn_s1_generated)

decode_subinsn_s2_generated = custom_target(
    'decode_subinsn_s2_generated.c.inc',
    output: 'decode_subinsn_s2_generated.c.inc',
    input: subinsn_s2_decode_generated,
    env: {'PYTHONPATH': meson.current_build_dir()},
    command: [python, files(decodetree_py), subinsn_s2_decode_generated, ['--static-decode=decode_subinsn_s2', '--insnwidth=16'], '-o', '@OUTPUT@'],
)
hexagon_ss.add(decode_subinsn_s2_generated)

#
# Generate the trans_* functions that the decoder will use
#
decodetree_trans_funcs_generated = custom_target(
    'decodetree_trans_funcs_generated.c.inc',
    output: 'decodetree_trans_funcs_generated.c.inc',
    depends: [iset_py, semantics_generated],
    env: {'PYTHONPATH': meson.current_build_dir()},
    command: [python, files('gen_trans_funcs.py'), semantics_generated, '@OUTPUT@'],
)
hexagon_ss.add(decodetree_trans_funcs_generated)
hexagon_softmmu_ss = ss.source_set()

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
    'mmvec/decode_ext_mmvec.c',
    'mmvec/system_ext_mmvec.c',
))

hexagon_softmmu_ss.add(files(
    'cpu_helper.c',
    'hex_mmu.c',
    'hex_interrupts.c',
    'hexswi.c',
    'machine.c',
))

helper_dep = [semantics_generated]
helper_in = [semantics_generated, gen_tcg_h, gen_tcg_hvx_h, gen_tcg_sys_h]

#
# Step 5
# We use Python scripts to generate the following files
#     helper_protos_generated.h.inc
#     helper_funcs_generated.c.inc
#     analyze_funcs_generated.c.inc
#
helper_protos_generated = custom_target(
    'helper_protos_generated.h.inc',
    output: 'helper_protos_generated.h.inc',
    depends: helper_dep,
    depend_files: [hex_common_py, gen_tcg_h, gen_tcg_hvx_h, gen_tcg_sys_h],
    command: [python, files('gen_helper_protos.py'), helper_in, '@OUTPUT@'],
)
hexagon_ss.add(helper_protos_generated)

helper_funcs_generated = custom_target(
    'helper_funcs_generated.c.inc',
    output: 'helper_funcs_generated.c.inc',
    depends: helper_dep,
    depend_files: [hex_common_py, gen_tcg_h, gen_tcg_hvx_h, gen_tcg_sys_h],
    command: [python, files('gen_helper_funcs.py'), helper_in, '@OUTPUT@'],
)
hexagon_ss.add(helper_funcs_generated)

analyze_funcs_generated = custom_target(
    'analyze_funcs_generated.c.inc',
    output: 'analyze_funcs_generated.c.inc',
    depends: helper_dep,
    depend_files: [hex_common_py, gen_tcg_h, gen_tcg_hvx_h, gen_tcg_sys_h],
    command: [python, files('gen_analyze_funcs.py'), helper_in, '@OUTPUT@'],
)
hexagon_ss.add(analyze_funcs_generated)

#
# Step 6
# If enabled, run helper-to-tcg to attempt to translate any remaining
# helper functions, producing:
#     helper-to-tcg-emitted.c
#     helper-to-tcg-emitted.h
#     helper-to-tcg-enabled
#     helper-to-tcg-log
#

idef_command_extra = []
idef_dep_extra = []
if helper_to_tcg.found()
    helper_to_tcg_get_llvm_ir_cmd = helper_to_tcg.get_variable('get_llvm_ir_cmd')
    helper_to_tcg_pipeline = helper_to_tcg.get_variable('pipeline')

    helper_to_tcg_input_files = [
        meson.current_source_dir() / 'op_helper.c',
        meson.current_source_dir() / 'translate.c',
        meson.current_source_dir() / 'reg_fields.c',
        meson.current_source_dir() / 'arch.c',
    ]

    ll = custom_target('to-ll',
        input: helper_to_tcg_input_files,
        output:'helper-to-tcg-input.ll',
        depends: [helper_funcs_generated, helper_protos_generated, libqemuutil],
        command: helper_to_tcg_get_llvm_ir_cmd + ['-o', '@OUTPUT@', '@INPUT@', '--target-path', 'target/hexagon']
    )

    helper_to_tcg_target = custom_target('helper-to-tcg-hexagon',
        output: ['helper-to-tcg-emitted.c',
                 'helper-to-tcg-emitted.h',
                 'helper-to-tcg-enabled'],
        input: [ll],
        depends: [helper_to_tcg_pipeline, analyze_funcs_generated, helper_funcs_generated, helper_protos_generated],
        command: [helper_to_tcg_pipeline,
                  '--temp-vector-block=tmp_VRegs',
                  '--max-vector-temp-bytes=1024',
                  '--max-vector-instructions=16',
                  '--mmu-index-function=get_tb_mmu_index',
                  '--tcg-global-mappings=tcg_global_mappings',
                  '--output-source=@OUTPUT0@',
                  '--output-header=@OUTPUT1@',
                  '--output-enabled=@OUTPUT2@',
                  '@INPUT@']
    )

    hexagon_ss.add(helper_to_tcg_target)

    # List of instructions for which TCG generation was successful
    generated_tcg_list = helper_to_tcg_target[2].full_path()

    # Setup dependencies for idef-parser
    idef_dep_extra += helper_to_tcg_target
    idef_command_extra += ['--helper-to-tcg', generated_tcg_list]

    # Setup input and dependencies for the final step, this depends on whether
    helper_dep += [helper_to_tcg_target]
    helper_in += ['--helper-to-tcg', generated_tcg_list]
endif



#
# Step 6
# We use flex/bison based idef-parser to generate TCG code for a lot
# of instructions. idef-parser outputs
#     idef-generated-emitter.c
#     idef-generated-emitter.h.inc
#     idef-generated-enabled-instructions
#

idef_parser_enabled = get_option('hexagon_idef_parser')
if idef_parser_enabled and ('hexagon-linux-user' in target_dirs or
                            'hexagon-softmmu' in target_dirs)
    idef_parser_input_generated = custom_target(
        'idef_parser_input.h.inc',
        output: 'idef_parser_input.h.inc',
        depends: [semantics_generated] + idef_dep_extra,
        depend_files: [hex_common_py],
        command: [python, files('gen_idef_parser_funcs.py'), semantics_generated, '@OUTPUT@'] + idef_command_extra
    )

    compiler = meson.get_compiler('c').cmd_array()
    preprocessed_idef_parser_input_generated = custom_target(
        'idef_parser_input.preprocessed.h.inc',
        output: 'idef_parser_input.preprocessed.h.inc',
        input: idef_parser_input_generated,
        depend_files: [idef_parser_dir / 'macros.h.inc'],
        command: compiler + ['-x', 'c', '-E', '-I', idef_parser_dir, '-o', '@OUTPUT@', '@INPUT@'],
    )

    flex = generator(
        find_program('flex'),
        output: ['@BASENAME@.yy.c', '@BASENAME@.yy.h'],
        arguments: ['-o', '@OUTPUT0@', '--header-file=@OUTPUT1@', '@INPUT@']
    )

    bison = generator(
        find_program('bison', version: '>=3.0'),
        output: ['@BASENAME@.tab.c', '@BASENAME@.tab.h'],
        arguments: ['@INPUT@', '--defines=@OUTPUT1@', '--output=@OUTPUT0@']
    )

    glib_dep = dependency('glib-2.0', native: true, static: false)

    idef_parser = executable(
        'idef-parser',
        [flex.process(idef_parser_dir / 'idef-parser.lex'),
         bison.process(idef_parser_dir / 'idef-parser.y'),
         idef_parser_dir / 'parser-helpers.c'],
        include_directories: ['idef-parser', '../../include/'],
        dependencies: [glib_dep],
        native: true
    )

    idef_generated_tcg = custom_target(
        'idef-generated-tcg',
        output: ['idef-generated-emitter.c',
                 'idef-generated-emitter.h.inc',
                 'idef-generated-enabled-instructions'],
        input: preprocessed_idef_parser_input_generated,
        depend_files: [hex_common_py],
        command: [idef_parser, '@INPUT@', '@OUTPUT0@', '@OUTPUT1@', '@OUTPUT2@']
    )

    indent = find_program('indent', required: false)
    if indent.found() and host_os == 'linux'
        idef_generated_tcg_c = custom_target(
            'indent',
            input: idef_generated_tcg[0],
            output: 'idef-generated-emitter.indented.c',
            command: [indent, '-linux', '@INPUT@', '-o', '@OUTPUT@']
        )
    else
        idef_generated_tcg_c = custom_target(
            'copy',
            input: idef_generated_tcg[0],
            output: 'idef-generated-emitter.indented.c',
            command: ['cp', '@INPUT@', '@OUTPUT@']
        )
    endif

    idef_generated_list = idef_generated_tcg[2].full_path()

    hexagon_ss.add(idef_generated_tcg_c)

    # Setup input and dependencies for the next step, this depends on whether or
    # not idef-parser is enabled
    helper_dep += [idef_generated_tcg_c, idef_generated_tcg]
    helper_in += ['--idef-parser', idef_generated_list]
endif

#
# Step 7
# We use Python scripts to generate the following files
#     tcg_funcs_generated.c.inc
#
tcg_funcs_generated = custom_target(
    'tcg_funcs_generated.c.inc',
    output: 'tcg_funcs_generated.c.inc',
    depends: helper_dep,
    depend_files: [hex_common_py, gen_tcg_h, gen_tcg_hvx_h, gen_tcg_sys_h],
    command: [python, files('gen_tcg_funcs.py'), helper_in, '@OUTPUT@'],
)
hexagon_ss.add(tcg_funcs_generated)

target_arch += {'hexagon': hexagon_ss}
target_system_arch += {'hexagon': hexagon_softmmu_ss}
