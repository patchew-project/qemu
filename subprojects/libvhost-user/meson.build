project('libvhost-user', 'c',
        license: 'GPL-2.0-or-later',
        default_options: ['c_std=gnu99'])

cc = meson.get_compiler('c')

glib = dependency('glib-2.0')
foreach h, syms: {
  'sys/mman.h': ['memfd_create', 'MFD_ALLOW_SEALING'],
  'sys/fcntl.h': ['F_SEAL_GROW', 'F_SEAL_SHRINK', 'F_SEAL_SEAL'] }
  foreach sym: syms
    cc.has_header_symbol(h, sym,
                         args: ['-D_GNU_SOURCE'],
                         required: true)
  endforeach
endforeach

inc = include_directories('../../include', '../../linux-headers')

vhost_user = static_library('vhost-user',
                            files('libvhost-user.c'),
                            include_directories: inc,
                            c_args: '-D_GNU_SOURCE')

vhost_user_glib = static_library('vhost-user-glib',
                                 files('libvhost-user-glib.c'),
                                 include_directories: inc,
                                 link_with: vhost_user,
                                 dependencies: glib)

vhost_user_dep = declare_dependency(link_with: vhost_user_glib,
                                    include_directories: include_directories('.'))
