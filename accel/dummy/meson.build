dummy_ss = ss.source_set()
dummy_ss.add(files(
  'dummy-cpus.c',
))

specific_ss.add_all(when: ['CONFIG_SOFTMMU', 'CONFIG_POSIX'], if_true: dummy_ss)
