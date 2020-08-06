if 'CONFIG_LIBISCSI' in config_host
  executable('vhost-user-scsi', files('vhost-user-scsi.c'),
             link_with: libvhost_user,
             dependencies: [qemuutil, libiscsi])
endif
