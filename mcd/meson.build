mcd_qapi_outputs = [
  'mcd-qapi-emit-events.c',
  'mcd-qapi-emit-events.h',
  'mcd-qapi-events.c',
  'mcd-qapi-events.h',
  'mcd-qapi-features.c',
  'mcd-qapi-features.h',
  'mcd-qapi-introspect.c',
  'mcd-qapi-introspect.h',
  'mcd-qapi-types.c',
  'mcd-qapi-types.h',
  'mcd-qapi-visit.c',
  'mcd-qapi-visit.h',
]

# QAPI outputs that will only be used by the MCD server
mcd_qapi_server_outputs = [
  'mcd-qapi-commands.c',
  'mcd-qapi-commands.h',
  'mcd-qapi-init-commands.c',
  'mcd-qapi-init-commands.h'
]

mcd_qapi_files = custom_target('MCD QAPI files',
                               output: mcd_qapi_outputs + mcd_qapi_server_outputs,
                               input: '../qapi/mcd.json',
                               command: [ qapi_gen, '-p', 'mcd-', '-o', 'mcd',
                                          '--suppress-tracing','@INPUT0@'],
                               depend_files: qapi_gen_depends)

mcd_qapi_ss = ss.source_set()

foreach f : mcd_qapi_files.to_list()
  if mcd_qapi_outputs.contains(fs.name(f))
    mcd_qapi_ss.add([f])
  endif
endforeach

mcd_qapi_ss.add(files('mcd_qapi.c'))
mcd_qapi_ss = mcd_qapi_ss.apply({})

libmcd_qapi = static_library('mcd_qapi',
                        mcd_qapi_ss.sources() + genh,
                        build_by_default: false)

mcd_qapi = declare_dependency(
    objects: libmcd_qapi.extract_all_objects(recursive: false))

foreach f : mcd_qapi_files.to_list()
  if mcd_qapi_server_outputs.contains(fs.name(f))
    libsystem_ss.add([f])
  endif
endforeach

libsystem_ss.add(files(
  'mcd_server.c',
  'mcd_stub.c',
  'mcd_monitor.c'))

libsystem_ss.add(mcd_qapi)
