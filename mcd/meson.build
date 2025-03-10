# MCD is only supported in system emulation
mcd_system_ss = ss.source_set()

mcd_system_ss.add(files('mcdserver.c', 'mcdstub_qapi.c', 'libmcd_qapi.c'))
mcd_system_ss = mcd_system_ss.apply({})

libmcd_system = static_library('mcd_system',
                                mcd_system_ss.sources() + genh,
                                build_by_default: false)

mcd_system = declare_dependency(objects: libmcd_system.extract_all_objects(recursive: false))
system_ss.add(mcd_system)
