mcd_qapi_outputs = [
  'mcd-qapi-commands.c',
  'mcd-qapi-commands.h',
  'mcd-qapi-emit-events.c',
  'mcd-qapi-emit-events.h',
  'mcd-qapi-events.c',
  'mcd-qapi-events.h',
  'mcd-qapi-features.c',
  'mcd-qapi-features.h',
  'mcd-qapi-init-commands.c',
  'mcd-qapi-init-commands.h',
  'mcd-qapi-introspect.c',
  'mcd-qapi-introspect.h',
  'mcd-qapi-types.c',
  'mcd-qapi-types.h',
  'mcd-qapi-visit.c',
  'mcd-qapi-visit.h',
]

mcd_qapi_files = custom_target('MCD QAPI files',
                               output: mcd_qapi_outputs,
                               input: '../qapi/mcd.json',
                               command: [ qapi_gen, '-p', 'mcd-', '-o', 'mcd',
                                          '--suppress-tracing','@INPUT0@'],
                               depend_files: qapi_gen_depends)

mcd_ss = ss.source_set()

mcd_ss.add(mcd_qapi_files.to_list())
mcd_ss.add(files(
  'mcd_server.c',
  'mcd_stub.c',
  'mcd_monitor.c'))

mcd_ss = mcd_ss.apply({})

libmcd = static_library('mcd_system',
                        mcd_ss.sources() + genh,
                        build_by_default: false)

mcd = declare_dependency(objects: libmcd.extract_all_objects(recursive: false))

system_ss.add(mcd)
