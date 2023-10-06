# We need to build the core mcd code via a library to be able to tweak
# cflags so:

mcd_system_ss = ss.source_set()

# only system emulation is supported over mcd
mcd_system_ss.add(files('mcdstub.c', '../debug/debug-mcd.c'))

mcd_system_ss = mcd_system_ss.apply(config_host, strict: false)

libmcd_softmmu = static_library('mcd_softmmu',
                                mcd_system_ss.sources() + genh,
                                name_suffix: 'fa',
                                build_by_default: have_system)

mcd_softmmu = declare_dependency(link_whole: libmcd_softmmu)
system_ss.add(mcd_softmmu)

common_ss.add(files('../debug/debug-common.c', 'mcd_syscalls.c'))
