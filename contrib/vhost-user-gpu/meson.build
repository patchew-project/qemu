if 'CONFIG_TOOLS' in config_host and 'CONFIG_VIRGL' in config_host \
    and 'CONFIG_GBM' in config_host and 'CONFIG_LINUX' in config_host \
    and pixman.found() and 'CONFIG_UI_DBUS' in config_host
  executable('vhost-user-gpu', files('vhost-user-gpu.c', 'virgl.c', 'vugbm.c') + dbus_display1,
             dependencies: [qemuutil, pixman, gbm, virgl, vhost_user, opengl, gio],
             install: true,
             install_dir: get_option('libexecdir'))

  configure_file(input: '50-qemu-gpu.json.in',
                 output: '50-qemu-gpu.json',
                 configuration: { 'libexecdir' : get_option('prefix') / get_option('libexecdir') },
                 install_dir: qemu_datadir / 'vhost-user')
endif
