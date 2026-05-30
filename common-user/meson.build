if not have_user
   subdir_done()
endif

common_user_inc += include_directories('host/' / host_arch)

user_ss.add(files(
  'mmap-min-addr.c',
  'safe-syscall.S',
  'safe-syscall-error.c',
  'selfmap.c',
))

if host_os == 'freebsd'
  libutil = cc.find_library('util', required: true)
  user_ss.add(libutil)
endif
