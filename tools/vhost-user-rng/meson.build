executable('vhost-user-rng', files(
  'main.c'),
  dependencies: [qemuutil, glib, gio, rt],
  install: true,
  install_dir: get_option('libexecdir'))

configure_file(input: '50-qemu-rng.json.in',
               output: '50-qemu-rng.json',
               configuration: config_host,
               install_dir: qemu_datadir / 'vhost-user')
