bsd_user_ss.add(files(
  'os-stat.c',
  'os-proc.c',
  'os-sys.c',
  'os-syscall.c',
))

bsd_syscall_nr = generator(sh,
			   arguments: [ meson.current_source_dir() / 'syscallhdr.sh', '@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@'],
			   output: '@BASENAME@_nr.h')
