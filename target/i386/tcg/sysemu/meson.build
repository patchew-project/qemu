files_ss = ss.source_set()
files_ss.add(when: ['CONFIG_TCG', 'CONFIG_SOFTMMU'], if_true: files(
  'tcg-cpu.c',
  'smm_helper.c',
  'excp_helper.c',
  'bpt_helper.c',
  'misc_helper.c',
  'fpu_helper.c',
  'svm_helper.c',
  'seg_helper.c',
))
tcg_module_ss.add_all(when: 'TARGET_I386', if_true: files_ss)
tcg_module_ss.add_all(when: 'TARGET_X86_64', if_true: files_ss)
