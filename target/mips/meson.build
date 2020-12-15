gen = [
  decodetree.process('isa-mips32r6.decode', extra_args: [ '--static-decode=decode_mips32r6' ]),
  decodetree.process('isa-mips64r6.decode', extra_args: [ '--static-decode=decode_mips64r6' ]),
  decodetree.process('mod-msa32.decode', extra_args: [ '--static-decode=decode_msa32' ]),
  decodetree.process('mod-msa64.decode', extra_args: [ '--static-decode=decode_msa64' ]),
]

mips_ss = ss.source_set()
mips_ss.add(gen)
mips_ss.add(files(
  'cpu.c',
  'gdbstub.c',
))
mips_ss.add(when: 'CONFIG_TCG', if_true: files(
  'dsp_helper.c',
  'fpu_helper.c',
  'isa-mips_rel6_translate.c',
  'lmmi_helper.c',
  'op_helper.c',
  'mod-msa_helper.c',
  'tlb_helper.c',
  'translate.c',
  'translate_addr_const.c',
  'mod-msa_translate.c',
))

mips_ss.add(when: 'CONFIG_KVM', if_true: files('kvm.c'))

mips_softmmu_ss = ss.source_set()
mips_softmmu_ss.add(files(
  'addr.c',
  'cp0_timer.c',
  'machine.c',
  'mips-semi.c',
))
mips_softmmu_ss.add(when: 'CONFIG_TCG', if_true: files(
  'cp0_helper.c',
))

target_arch += {'mips': mips_ss}
target_softmmu_arch += {'mips': mips_softmmu_ss}
