# FIXME extra_args should accept files()
gen = [
  decodetree.process('insn16.decode', extra_args: ['--static-decode=decode_insn16', '--insnwidth=16']),
  decodetree.process('insn32.decode', extra_args: '--static-decode=decode_insn32'),
  decodetree.process('xthead.decode', extra_args: '--static-decode=decode_xthead'),
  decodetree.process('XVentanaCondOps.decode', extra_args: '--static-decode=decode_XVentanaCodeOps'),
]
riscv_ss.add(when: 'CONFIG_TCG', if_true: gen)

riscv_ss.add(when: 'CONFIG_TCG', if_true: files(
  'cpu.c',
  'fpu_helper.c',
  'op_helper.c',
  'vector_helper.c',
  'bitmanip_helper.c',
  'translate.c',
  'm128_helper.c',
  'crypto_helper.c',
  'zce_helper.c',
), if_false: files('tcg-stub.c'))

subdir('sysemu')
