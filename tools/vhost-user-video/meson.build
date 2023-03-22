executable('vhost-user-video', files(
  'vhost-user-video.c', 'v4l2_backend.c', 'virtio_video_helpers.c', 'virtio_video_udmabuf.c'),
  dependencies: [qemuutil, glib, gio, vhost_user],
  install: true,
  install_dir: get_option('libexecdir'))

configure_file(input: '50-qemu-video.json.in',
               output: '50-qemu-video.json',
               configuration: config_host,
               install_dir: qemu_datadir / 'vhost-user')
