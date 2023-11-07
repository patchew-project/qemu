# only system emulation is supported over mcd
mcd_system_ss = ss.source_set()
mcd_system_ss.add(files('mcdstub.c', '../debug/debug-mcd.c'))
mcd_system_ss = mcd_system_ss.apply(config_host, strict: false)

libmcd_system = static_library('mcd_system',
                                mcd_system_ss.sources() + genh,
                                name_suffix: 'fa',
                                build_by_default: have_system)

mcd_system = declare_dependency(link_whole: libmcd_system)
system_ss.add(mcd_system)

# used for gdb (user/system) and mcd:
common_ss.add(files('../debug/debug-common.c'))
