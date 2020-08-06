specific_ss.add(when: 'CONFIG_SOFTMMU', if_true: files(
  'arch_init.c',
  'balloon.c',
  'cpus.c',
  'cpu-throttle.c',
  'ioport.c',
  'memory.c',
  'memory_mapping.c',
  'qtest.c',
  'vl.c'))

softmmu_emulator_ss.add(files(
  'main.c',
), sdl)
