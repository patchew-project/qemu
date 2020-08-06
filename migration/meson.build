softmmu_ss.add(files(
  'block-dirty-bitmap.c',
  'channel.c',
  'colo-failover.c',
  'colo.c',
  'exec.c',
  'fd.c',
  'global_state.c',
  'migration.c',
  'multifd.c',
  'multifd-zlib.c',
  'page_cache.c',
  'postcopy-ram.c',
  'qemu-file-channel.c',
  'qemu-file.c',
  'qjson.c',
  'savevm.c',
  'socket.c',
  'tls.c',
  'vmstate-types.c',
  'vmstate.c',
  'xbzrle.c',
))

softmmu_ss.add(when: ['CONFIG_RDMA', rdma], if_true: files('rdma.c'))
softmmu_ss.add(when: 'CONFIG_LIVE_BLOCK_MIGRATION', if_true: files('block.c'))
softmmu_ss.add(when: 'CONFIG_ZSTD', if_true: files('multifd-zstd.c'))

specific_ss.add(when: 'CONFIG_SOFTMMU', if_true: [files('ram.c')])
