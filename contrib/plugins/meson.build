contrib_plugins = [
'bbv.c',
'cache.c',
'cflow.c',
'drcov.c',
'execlog.c',
'hotblocks.c',
'hotpages.c',
'howvec.c',
'hwprofile.c',
'ips.c',
'stoptrigger.c',
'traps.c',
'uftrace.c',
]

if host_os != 'windows'
  # lockstep uses socket.h
  contrib_plugins += 'lockstep.c'
endif

if host_os != 'windows' and host_os != 'darwin'
  # The syscall-filter demos assume little-endian direct-pointer access.
  if host_machine.endian() == 'little'
    contrib_plugins += 'syscall_filter_zlib.c'
    if supported_backends.contains('ucontext')
      contrib_plugins += 'syscall_filter_callback_qsort.c'
    endif
  endif
endif

if 'cpp' in all_languages
  contrib_plugins += 'cpp.cpp'
endif

t = []
if get_option('plugins')
  foreach i : contrib_plugins
    deps = plugins_deps
    if i == 'syscall_filter_zlib.c'
      deps = [plugins_deps, zlib]
    endif
    t += shared_module(fs.stem(i), files(i),
                       dependencies: deps)
  endforeach
endif
if t.length() > 0
  alias_target('contrib-plugins', t)
else
  run_target('contrib-plugins', command: [python, '-c', ''])
endif

plugin_modules += t
