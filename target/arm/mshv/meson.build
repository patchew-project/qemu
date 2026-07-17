arm_mshv_ss = ss.source_set()

arm_mshv_ss.add(files(
  'mshv-all.c',
))

arm_system_ss.add_all(when: 'CONFIG_MSHV', if_true: arm_mshv_ss)
