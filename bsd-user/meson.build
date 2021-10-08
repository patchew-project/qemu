if not config_target.has_key('CONFIG_BSD_USER')
   subdir_done()
endif

bsd_user_ss.add(files(
  'bsdload.c',
  'elfload.c',
  'main.c',
  'mmap.c',
  'signal.c',
  'strace.c',
  'syscall.c',
  'uaccess.c',
))
