gunyah_ss = ss.source_set()
gunyah_ss.add(files(
  'gunyah-all.c',
  'gunyah-accel-ops.c',
))

specific_ss.add_all(when: 'CONFIG_GUNYAH', if_true: gunyah_ss)
