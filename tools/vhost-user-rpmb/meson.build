executable('vhost-user-rpmb', files(
  'main.c'),
  dependencies: [glib],
  link_with: [libvhost_user],
  install: true,
  install_dir: get_option('libexecdir'))

configure_file(input: '50-qemu-rpmb.json.in',
               output: '50-qemu-rpmb.json',
               configuration: config_host,
               install_dir: qemu_datadir / 'vhost-user')
