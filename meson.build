project('qemu', 'c', meson_version: '>=0.50.999')

kconfig = import('unstable-kconfig')
config_host = kconfig.load(meson.current_build_dir() / 'config-host.mak')

add_project_arguments(config_host['QEMU_CFLAGS'].split(),
                      language: 'c')
add_project_arguments(config_host['QEMU_INCLUDES'].split(),
                      language: 'c')

subdir('contrib/libvhost-user')
