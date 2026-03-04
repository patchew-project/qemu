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

if 'cpp' in all_languages
  contrib_plugins += 'cpp.cpp'
endif

t = []
if get_option('plugins')
  foreach i : contrib_plugins
    if i == 'cpp.cpp' and cxx.has_argument('-fcoroutines')
      cpp_args = ['-fcoroutines']
    else
      cpp_args = []
    endif

    t += shared_module(fs.stem(i), files(i),
                       cpp_args: cpp_args, dependencies: plugins_deps)
  endforeach
endif
if t.length() > 0
  alias_target('contrib-plugins', t)
else
  run_target('contrib-plugins', command: [python, '-c', ''])
endif

plugin_modules += t
