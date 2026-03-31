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

if host_os != 'windows' and host_os != 'darwin' and
   host_machine.endian() == 'little'
  # The zlib syscall-filter demo assumes little-endian direct-pointer access.
  contrib_plugins += 'syscall_filter_zlib.c'
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
