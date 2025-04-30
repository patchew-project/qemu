mcd_ss = ss.source_set()

mcd_ss.add(files(
  'mcd_server.c'))

mcd_ss = mcd_ss.apply({})

libmcd = static_library('mcd_system',
                        mcd_ss.sources() + genh,
                        build_by_default: false)

mcd = declare_dependency(objects: libmcd.extract_all_objects(recursive: false))

system_ss.add(mcd)
