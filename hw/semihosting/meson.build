specific_ss.add(when: 'CONFIG_SEMIHOSTING', if_true: files(
  'config.c',
  'console.c',
))

specific_ss.add(when: 'CONFIG_TCG', if_true: files ('common-semi.c'))
