common_ss.add(when: 'CONFIG_ALPHA_DIS', if_true: files('alpha.c'))
common_ss.add(when: 'CONFIG_HEXAGON_DIS', if_true: files('hexagon.c'))
common_ss.add(when: 'CONFIG_HPPA_DIS', if_true: files('hppa.c'))
common_ss.add(when: 'CONFIG_M68K_DIS', if_true: files('m68k.c'))
common_ss.add(when: 'CONFIG_MICROBLAZE_DIS', if_true: files('microblaze.c'))
common_ss.add(when: 'CONFIG_MIPS_DIS', if_true: files('mips.c', 'nanomips.c'))
common_ss.add(when: 'CONFIG_RISCV_DIS', if_true: files(
    'riscv.c',
    'riscv-xthead.c',
    'riscv-xventana.c'
))
x86_data = files(
    'x86-data/x86_adx.csv',
    'x86-data/x86_aes.csv',
    'x86-data/x86_aeskle.csv',
    'x86-data/x86_aesklewide.csv',
    'x86-data/x86_avx2.csv',
    'x86-data/x86_avx5124fmaps.csv',
    'x86-data/x86_avx5124vnniw.csv',
    'x86-data/x86_avx512b.csv',
    'x86-data/x86_avx512bf16.csv',
    'x86-data/x86_avx512bitalg.csv',
    'x86-data/x86_avx512bw.csv',
    'x86-data/x86_avx512cd.csv',
    'x86-data/x86_avx512d.csv',
    'x86-data/x86_avx512dq.csv',
    'x86-data/x86_avx512er.csv',
    'x86-data/x86_avx512f.csv',
    'x86-data/x86_avx512fp16.csv',
    'x86-data/x86_avx512ifma.csv',
    'x86-data/x86_avx512pf.csv',
    'x86-data/x86_avx512vbmi2.csv',
    'x86-data/x86_avx512vbmi.csv',
    'x86-data/x86_avx512vl.csv',
    'x86-data/x86_avx512vnni.csv',
    'x86-data/x86_avx512vp2intersect.csv',
    'x86-data/x86_avx512vpopcntdq.csv',
    'x86-data/x86_avx.csv',
    'x86-data/x86_avxneconvert.csv',
    'x86-data/x86_avxvnni.csv',
    'x86-data/x86_avxvnniint8.csv',
    'x86-data/x86_base.csv',
    'x86-data/x86_bmi1.csv',
    'x86-data/x86_bmi2.csv',
    'x86-data/x86_cet.csv',
    'x86-data/x86_cldemote.csv',
    'x86-data/x86_clwb.csv',
    'x86-data/x86_enqcmd.csv',
    'x86-data/x86_f16c.csv',
    'x86-data/x86_fma.csv',
    'x86-data/x86_fsgsbase.csv',
    'x86-data/x86_gfni.csv',
    'x86-data/x86_hreset.csv',
    'x86-data/x86_invpcid.csv',
    'x86-data/x86_lzcnt.csv',
    'x86-data/x86_mmx.csv',
    'x86-data/x86_movdir64b.csv',
    'x86-data/x86_movdiri.csv',
    'x86-data/x86_mpx.csv',
    'x86-data/x86_msrlist.csv',
    'x86-data/x86_ospke.csv',
    'x86-data/x86_pclmulqdq.csv',
    'x86-data/x86_pconfig.csv',
    'x86-data/x86_prefetchw.csv',
    'x86-data/x86_raoint.csv',
    'x86-data/x86_rdpid.csv',
    'x86-data/x86_rdrand.csv',
    'x86-data/x86_rdseed.csv',
    'x86-data/x86_rtm.csv',
    'x86-data/x86_serialize.csv',
    'x86-data/x86_sha.csv',
    'x86-data/x86_smap.csv',
    'x86-data/x86_sse2.csv',
    'x86-data/x86_sse3.csv',
    'x86-data/x86_sse4_1.csv',
    'x86-data/x86_sse4_2.csv',
    'x86-data/x86_sse4_3.csv',
    'x86-data/x86_sse.csv',
    'x86-data/x86_ssse3.csv',
    'x86-data/x86_uintr.csv',
    'x86-data/x86_vaes.csv',
    'x86-data/x86_vmx.csv',
    'x86-data/x86_waitpkg.csv',
    'x86-data/x86_wbnoinvd.csv',
    'x86-data/x86_x87.csv',
    'x86-data/x86_xsaveopt.csv'
)
x86_tablegen_py = find_program('../scripts/x86-tablegen.py')
x86_gen_enums_inc = custom_target(
    'x86-enums.inc',
    output: 'x86-enums.inc',
    depend_files: x86_data,
    command: [x86_tablegen_py, '--print-opcode-enums',
              '--output-file', '@OUTPUT@', x86_data]
)
x86_gen_tables_inc = custom_target(
    'x86-tables.inc',
    output: 'x86-tables.inc',
    depend_files: x86_data,
    command: [x86_tablegen_py, '--print-opcode-tables',
              '--output-file', '@OUTPUT@', x86_data]
)
common_ss.add(when: 'CONFIG_I386_DIS', if_true: x86_gen_enums_inc)
common_ss.add(when: 'CONFIG_I386_DIS', if_true: x86_gen_tables_inc)
common_ss.add(when: 'CONFIG_I386_DIS', if_true: files(
    'x86-core.c',
    'x86-disas.c'
))
common_ss.add(when: 'CONFIG_SH4_DIS', if_true: files('sh4.c'))
common_ss.add(when: 'CONFIG_SPARC_DIS', if_true: files('sparc.c'))
common_ss.add(when: 'CONFIG_XTENSA_DIS', if_true: files('xtensa.c'))
common_ss.add(when: capstone, if_true: [files('capstone.c'), capstone])
common_ss.add(when: 'CONFIG_TCG', if_true: files(
    'disas-host.c',
    'disas-target.c',
    'objdump.c'
))
common_ss.add(files('disas-common.c'))
system_ss.add(files('disas-mon.c'))
specific_ss.add(capstone)
