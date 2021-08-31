files_ss = ss.source_set()
files_ss.add(when: 'CONFIG_TCG', if_true: files(
  'bpt_helper.c',
  'cc_helper.c',
  'excp_helper.c',
  'fpu_helper.c',
  'int_helper.c',
  'mem_helper.c',
  'misc_helper.c',
  'mpx_helper.c',
  'seg_helper.c',
  'tcg-cpu.c',
  'translate.c'))
tcg_module_ss.add_all(when: 'TARGET_I386', if_true: files_ss)
tcg_module_ss.add_all(when: 'TARGET_X86_64', if_true: files_ss)

subdir('sysemu')
subdir('user')
