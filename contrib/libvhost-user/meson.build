libvhost_user = static_library('vhost-user',
               [files('libvhost-user.c', 'libvhost-user-glib.c'),
                meson.source_root() / 'stubs/error-printf.c',
                meson.source_root() / 'stubs/monitor.c',
                meson.source_root() / 'util/error.c',
                meson.source_root() / 'util/qemu-error.c',
                meson.source_root() / 'util/memfd.c'])
