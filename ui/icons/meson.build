
png_icon_sizes = [
    '16x16',
    '24x24',
    '32x32',
    '48x48',
    '64x64',
    '128x128',
    '256x256',
    '512x512',
]

foreach icon_size: png_icon_sizes
  install_data(
    join_paths(icon_size, 'apps', 'qemu.png'),
    install_dir: join_paths(qemu_icondir, 'hicolor', icon_size, 'apps')
  )
endforeach

install_data('32x32/apps/qemu.bmp',
             install_dir: qemu_icondir / 'hicolor' / '32x32' / 'apps')

install_data('scalable/apps/qemu.svg',
             install_dir: qemu_icondir / 'hicolor' / 'scalable' / 'apps')
