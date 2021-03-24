executable('vhost-user-i2c', files(
  'main.c'),
  dependencies: [qemuutil, glib, gio],
  install: true,
  install_dir: get_option('libexecdir'))

configure_file(input: '50-qemu-i2c.json.in',
               output: '50-qemu-i2c.json',
               configuration: config_host,
               install_dir: qemu_datadir / 'vhost-user')
