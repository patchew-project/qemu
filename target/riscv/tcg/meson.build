if 'CONFIG_TCG' not in config_all_accel
  subdir_done()
endif

riscv_ss.add(files(
  'bitmanip_helper.c',
 'cpu_helper.c',
  'csr.c',
  'crypto_helper.c',
  'fpu_helper.c',
  'm128_helper.c',
  'op_helper.c',

  # In theory the RISC-V HW e-trace support is system_ss only,
  # but we're instrumeting TCG code that is visible to riscv_ss
  # ae well, ergo the helper needs to be visible in user-mode too.
  'trace_helper.c',

  'translate.c',
  'tcg-cpu.c',
  'vcrypto_helper.c',
  'vector_helper.c',
  'vector_internals.c',
  'xsf_helper.c',
  'zce_helper.c'))


riscv_system_ss.add(files(
  'debug.c',
  'mips_csr.c',
  'pmp.c',
  'pmu.c',
  'th_csr.c',
))


