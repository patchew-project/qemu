project('qemu', 'c', meson_version: '>=0.50.999')
cc = meson.get_compiler('c')

kconfig = import('unstable-kconfig')
config_host = kconfig.load(meson.current_build_dir() / 'config-host.mak')

add_project_arguments(config_host['QEMU_CFLAGS'].split(),
                      language: 'c')
add_project_arguments(config_host['QEMU_INCLUDES'].split(),
                      language: 'c')

glib = declare_dependency(compile_args: config_host['GLIB_CFLAGS'].split(),
                          link_args: config_host['GLIB_LIBS'].split())
libiscsi = declare_dependency(compile_args: config_host['LIBISCSI_CFLAGS'].split(),
                          link_args: config_host['LIBISCSI_LIBS'].split())

subdir('contrib/libvhost-user')
subdir('contrib/rdmacm-mux')
subdir('contrib/vhost-user-blk')
subdir('contrib/vhost-user-scsi')
