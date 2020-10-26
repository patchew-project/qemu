specific_ss.add(when: 'CONFIG_SEMIHOSTING', if_true: files(
  'config.c',
  'console.c',
))

specific_ss.add(when: 'CONFIG_TCG', if_true: files ('common-semi.c'))
specific_ss.add(when: 'TARGET_RISCV32', if_true: files ('common-semi.c'))
specific_ss.add(when: 'TARGET_RISCV64', if_true: files ('common-semi.c'))
