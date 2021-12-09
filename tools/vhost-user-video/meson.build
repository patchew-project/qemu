executable('vhost-user-video', files(
  'main.c', 'v4l2_backend.c', 'virtio_video_helpers.c'),
  dependencies: [qemuutil, glib, gio, vhost_user],
  install: true,
  install_dir: get_option('libexecdir'))

configure_file(input: '50-qemu-rpmb.json.in',
               output: '50-qemu-rpmb.json',
               configuration: config_host,
               install_dir: qemu_datadir / 'vhost-user')
