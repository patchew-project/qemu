cris_ss = ss.source_set()
cris_ss.add(files(
  'cpu.c',
  'gdbstub.c',
  'helper.c',
  'op_helper.c',
  'translate.c',
))

cris_softmmu_ss = ss.source_set()
cris_softmmu_ss.add(files('mmu.c', 'machine.c'))

cris_user_ss = ss.source_set()

target_arch += {'cris': cris_ss}
target_softmmu_arch += {'cris': cris_softmmu_ss}
target_user_arch += {'cris': cris_user_ss}
