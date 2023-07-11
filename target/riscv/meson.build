riscv_ss = ss.source_set()
riscv_system_ss = ss.source_set()

riscv_ss.add(files(
  'cpu.c',
  'cpu_helper.c',
  'csr.c',
  'gdbstub.c',
))

riscv_system_ss.add(files(
  'debug.c',
))

subdir('tcg')
subdir('sysemu')

target_arch += {'riscv': riscv_ss}
target_softmmu_arch += {'riscv': riscv_system_ss}
