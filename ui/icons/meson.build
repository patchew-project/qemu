icons = [
  {
    'source': 'qemu_32x32.bmp',
    'install': 'hicolor' / '32x32' / 'apps' / 'qemu.bmp',
  },
  {
    'source': 'qemu.svg',
    'install': 'hicolor' / 'scalable' / 'apps' / 'qemu.svg',
  },
]

foreach s: [16, 24, 32, 48, 64, 128, 256, 512]
  s = '@0@x@0@'.format(s.to_string())
  icons += {
    'source': 'qemu_@0@.png'.format(s),
    'install': 'hicolor' / s / 'apps' / 'qemu.png',
  }
endforeach

foreach icon: icons
  source = icon.get('source')
  install = icon.get('install')

  install_data(source,
               rename: fs.name(install),
               install_dir: qemu_icondir / fs.parent(install))

  run_command('mkdir', '-p', qemu_bundledir / qemu_icondir / fs.parent(install),
              check: true)

  run_command('ln', '-sf', meson.current_source_dir() / source, qemu_bundledir / qemu_icondir / install,
              check: true)
endforeach
