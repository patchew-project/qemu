system_ss.add([files('dump.c', 'dump-hmp-cmds.c'), snappy, lzo])
if host_os == 'windows'
  system_ss.add(files('win_dump.c'))
else
  system_ss.add(files('win_dump-stubs.c'))
endif
