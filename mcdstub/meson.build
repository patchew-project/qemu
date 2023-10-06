#
# The main gdbstub still relies on per-build definitions of various
# types. The bits pushed to softmmu/user.c try to use guest agnostic
# types such as hwaddr.
#

# We need to build the core mcd code via a library to be able to tweak
# cflags so:

mcd_system_ss = ss.source_set()

# We build one version of the mcdstub,
#because it only needs to work for system emulation
mcd_system_ss.add(files('mcdstub.c'))

mcd_system_ss = mcd_system_ss.apply(config_host, strict: false)

libmcd_softmmu = static_library('mcd_softmmu',
                                mcd_system_ss.sources() + genh,
                                name_suffix: 'fa',
                                build_by_default: have_system)

mcd_softmmu = declare_dependency(link_whole: libmcd_softmmu)
system_ss.add(mcd_softmmu)

# this might cause problems because we don't support user mode
common_ss.add(files('mcd_syscalls.c'))
