chardev_ss = ss.source_set()
chardev_ss.add(files(
  'char-fe.c',
  'char-file.c',
  'char-io.c',
  'char-mux.c',
  'char-null.c',
  'char-pipe.c',
  'char-ringbuf.c',
  'char-serial.c',
  'char-socket.c',
  'char-stdio.c',
  'char-udp.c',
  'char.c',
))
chardev_ss.add(when: 'CONFIG_POSIX', if_true: files(
  'char-fd.c',
  'char-parallel.c',
  'char-pty.c',
))
chardev_ss.add(when: 'CONFIG_WIN32', if_true: files(
  'char-console.c',
  'char-win-stdio.c',
  'char-win.c',
))

chardev_ss = chardev_ss.apply(config_host, strict: false)
libchardev = static_library('chardev', chardev_ss.sources(),
                            name_suffix: 'fa',
                            build_by_default: false)

chardev = declare_dependency(link_whole: libchardev)

softmmu_ss.add(files('chardev-sysemu.c', 'msmouse.c', 'wctablet.c', 'testdev.c'))
softmmu_ss.add(when: ['CONFIG_SPICE', spice], if_true: files('spice.c'))

chardev_modules = []

if config_host.has_key('CONFIG_BRLAPI') and config_host.has_key('CONFIG_SDL')
  chardev_modules += [['baum', files('baum.c'), [sdl, brlapi]]]
endif

modules += { 'chardev': chardev_modules }
