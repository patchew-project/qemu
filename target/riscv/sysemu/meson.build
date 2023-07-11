riscv_system_ss.add(files(
  'arch_dump.c',
  'machine.c',
  'monitor.c',
  'pmp.c',
  'pmu.c',
  'riscv-qmp-cmds.c',
  'time_helper.c',
))

riscv_ss.add(when: 'CONFIG_KVM', if_true: files('kvm.c'))
