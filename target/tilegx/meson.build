tilegx_ss = ss.source_set()
tilegx_ss.add(files(
  'cpu.c',
  'helper.c',
  'simd_helper.c',
  'translate.c',
))
tilegx_ss.add(zlib)

tilegx_softmmu_ss = ss.source_set()

tilegx_user_ss = ss.source_set()

target_arch += {'tilegx': tilegx_ss}
target_softmmu_arch += {'tilegx': tilegx_softmmu_ss}
target_user_arch += {'tilegx': tilegx_user_ss}
