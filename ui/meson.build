softmmu_ss.add(pixman)
specific_ss.add(when: ['CONFIG_SOFTMMU'], if_true: pixman)   # for the include path

softmmu_ss.add(files(
  'clipboard.c',
  'console.c',
  'cursor.c',
  'input-keymap.c',
  'input-legacy.c',
  'input-barrier.c',
  'input.c',
  'kbd-state.c',
  'keymaps.c',
  'qemu-pixman.c',
  'udmabuf.c',
))
softmmu_ss.add([spice_headers, files('spice-module.c')])
softmmu_ss.add(when: spice_protocol, if_true: files('vdagent.c'))

softmmu_ss.add(when: 'CONFIG_LINUX', if_true: files('input-linux.c'))
softmmu_ss.add(when: cocoa, if_true: files('cocoa.m'))

vnc_ss = ss.source_set()
vnc_ss.add(files(
  'vnc.c',
  'vnc-enc-zlib.c',
  'vnc-enc-hextile.c',
  'vnc-enc-tight.c',
  'vnc-palette.c',
  'vnc-enc-zrle.c',
  'vnc-auth-vencrypt.c',
  'vnc-ws.c',
  'vnc-jobs.c',
  'vnc-clipboard.c',
))
vnc_ss.add(zlib, png, jpeg, gnutls)
vnc_ss.add(when: sasl, if_true: files('vnc-auth-sasl.c'))
softmmu_ss.add_all(when: vnc, if_true: vnc_ss)
softmmu_ss.add(when: vnc, if_false: files('vnc-stubs.c'))
specific_ss.add(when: ['CONFIG_SOFTMMU'], if_true: opengl)

ui_modules = {}

if curses.found()
  curses_ss = ss.source_set()
  curses_ss.add(when: [curses, iconv], if_true: [files('curses.c'), pixman])
  ui_modules += {'curses' : curses_ss}
endif

if config_host.has_key('CONFIG_OPENGL')
  opengl_ss = ss.source_set()
  opengl_ss.add(gbm)
  opengl_ss.add(when: [opengl, pixman, 'CONFIG_OPENGL'],
               if_true: files('shader.c', 'console-gl.c', 'egl-helpers.c', 'egl-context.c'))
  ui_modules += {'opengl' : opengl_ss}
endif

if config_host.has_key('CONFIG_OPENGL') and gbm.found()
  egl_headless_ss = ss.source_set()
  egl_headless_ss.add(when: [opengl, gbm, pixman, 'CONFIG_OPENGL'],
                      if_true: files('egl-headless.c'))
  ui_modules += {'egl-headless' : egl_headless_ss}
endif

wayland_scanner = find_program('wayland-scanner')
proto_sources = [
  ['xdg-shell', 'stable', ],
  ['fullscreen-shell', 'unstable', 'v1', ],
  ['linux-dmabuf', 'unstable', 'v1', ],
]
wayland_headers = []
wayland_proto_sources = []

if wayland.found()
  foreach p: proto_sources
    proto_name = p.get(0)
    proto_stability = p.get(1)

    if proto_stability == 'stable'
      output_base = proto_name
      input = files(join_paths(wlproto_dir, '@0@/@1@/@2@.xml'.format(proto_stability, proto_name, output_base)))
    else
      proto_version = p.get(2)
      output_base = '@0@-@1@-@2@'.format(proto_name, proto_stability, proto_version)
      input = files(join_paths(wlproto_dir, '@0@/@1@/@2@.xml'.format(proto_stability, proto_name, output_base)))
    endif

    wayland_headers += custom_target('@0@ client header'.format(output_base),
      input: input,
      output: '@0@-client-protocol.h'.format(output_base),
      command: [
        wayland_scanner,
        'client-header',
        '@INPUT@', '@OUTPUT@',
      ], build_by_default: true
    )

    wayland_proto_sources += custom_target('@0@ source'.format(output_base),
      input: input,
      output: '@0@-protocol.c'.format(output_base),
      command: [
        wayland_scanner,
        'private-code',
        '@INPUT@', '@OUTPUT@',
      ], build_by_default: true
    )
  endforeach
endif

if wayland.found()
  wayland_ss = ss.source_set()
  wayland_ss.add(when: wayland, if_true: files('wayland.c', 'xdg-shell-protocol.c', 'fullscreen-shell-unstable-v1-protocol.c','linux-dmabuf-unstable-v1-protocol.c'))
  #wayland_ss.add(when: wayland, if_true: files('wayland.c'), [wayland_proto_sources])
  ui_modules += {'wayland' : wayland_ss}
endif

if gtk.found()
  softmmu_ss.add(when: 'CONFIG_WIN32', if_true: files('win32-kbd-hook.c'))

  gtk_ss = ss.source_set()
  gtk_ss.add(gtk, vte, pixman, files('gtk.c', 'gtk-clipboard.c'))
  gtk_ss.add(when: x11, if_true: files('x_keymap.c'))
  gtk_ss.add(when: [opengl, 'CONFIG_OPENGL'], if_true: files('gtk-gl-area.c'))
  gtk_ss.add(when: [x11, opengl, 'CONFIG_OPENGL'], if_true: files('gtk-egl.c'))
  ui_modules += {'gtk' : gtk_ss}
endif

if sdl.found()
  softmmu_ss.add(when: 'CONFIG_WIN32', if_true: files('win32-kbd-hook.c'))

  sdl_ss = ss.source_set()
  sdl_ss.add(sdl, sdl_image, pixman, glib, files(
    'sdl2-2d.c',
    'sdl2-input.c',
    'sdl2.c',
  ))
  sdl_ss.add(when: [opengl, 'CONFIG_OPENGL'], if_true: files('sdl2-gl.c'))
  sdl_ss.add(when: x11, if_true: files('x_keymap.c'))
  ui_modules += {'sdl' : sdl_ss}
endif

if config_host.has_key('CONFIG_SPICE')
  spice_core_ss = ss.source_set()
  spice_core_ss.add(spice, pixman, files(
    'spice-core.c',
    'spice-input.c',
    'spice-display.c'
  ))
  ui_modules += {'spice-core' : spice_core_ss}
endif

if config_host.has_key('CONFIG_SPICE') and config_host.has_key('CONFIG_GIO')
  spice_ss = ss.source_set()
  spice_ss.add(spice, gio, pixman, files('spice-app.c'))
  ui_modules += {'spice-app': spice_ss}
endif

keymap_gen = find_program('keycodemapdb/tools/keymap-gen')

keymaps = [
  ['atset1', 'qcode'],
  ['linux', 'qcode'],
  ['qcode', 'atset1'],
  ['qcode', 'atset2'],
  ['qcode', 'atset3'],
  ['qcode', 'linux'],
  ['qcode', 'qnum'],
  ['qcode', 'sun'],
  ['qnum', 'qcode'],
  ['usb', 'qcode'],
  ['win32', 'qcode'],
  ['x11', 'qcode'],
  ['xorgevdev', 'qcode'],
  ['xorgkbd', 'qcode'],
  ['xorgxquartz', 'qcode'],
  ['xorgxwin', 'qcode'],
  ['osx', 'qcode'],
]

if have_system or xkbcommon.found()
  foreach e : keymaps
    output = 'input-keymap-@0@-to-@1@.c.inc'.format(e[0], e[1])
    genh += custom_target(output,
                  output: output,
                  capture: true,
                  input: files('keycodemapdb/data/keymaps.csv'),
                  command: [python.full_path(), files('keycodemapdb/tools/keymap-gen'),
                            'code-map',
                            '--lang', 'glib2',
                            '--varname', 'qemu_input_map_@0@_to_@1@'.format(e[0], e[1]),
                            '@INPUT0@', e[0], e[1]])
  endforeach
endif

subdir('shader')

if have_system
  subdir('icons')

  install_data('qemu.desktop', install_dir: qemu_desktopdir)
endif

modules += {'ui': ui_modules}
