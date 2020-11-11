arc_softmmu_ss = ss.source_set()
arc_softmmu_ss.add(files(
  'translate.c',
  'helper.c',
  'cpu.c',
  'op_helper.c',
  'gdbstub.c',
  'decoder.c',
  'regs.c',
  'semfunc.c',
  'semfunc-helper.c',
  'mmu.c',
  'mpu.c',
  'timer.c',
  'irq.c',
  'cache.c',
))

target_arch += {'arc': arc_softmmu_ss}
target_softmmu_arch += {'arc': arc_softmmu_ss}
