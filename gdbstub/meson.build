#
# The main gdbstub still relies on per-build definitions of various
# types. The bits pushed to softmmu/user.c try to use guest agnostic
# types such as hwaddr.
#

# We need to build the core gdb code via a library to be able to tweak
# cflags so:

gdb_user_ss = ss.source_set()
gdb_softmmu_ss = ss.source_set()

# We build two versions of gdbstub, one for each mode
gdb_user_ss.add(files('gdbstub.c'))
gdb_softmmu_ss.add(files('gdbstub.c'))

gdb_user_ss = gdb_user_ss.apply(config_host, strict: false)
gdb_softmmu_ss = gdb_softmmu_ss.apply(config_host, strict: false)

libgdb_user = static_library('gdb_user', gdb_user_ss.sources(),
                             name_suffix: 'fa',
                             c_args: '-DCONFIG_USER_ONLY')

libgdb_softmmu = static_library('gdb_softmmu', gdb_softmmu_ss.sources(),
                                name_suffix: 'fa')

gdb_user = declare_dependency(link_whole: libgdb_user)
user_ss.add(gdb_user)
gdb_softmmu = declare_dependency(link_whole: libgdb_softmmu)
softmmu_ss.add(gdb_softmmu)

# The rest of the mode specific code can be added directly
user_ss.add(files('user.c'))
softmmu_ss.add(files('softmmu.c'))

# These have to built to the target ABI
specific_ss.add(files('syscalls.c'))

# and BSD?
specific_ss.add(when: 'CONFIG_LINUX_USER', if_true: files('user-target.c'))
