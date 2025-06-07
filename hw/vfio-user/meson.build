vfio_user_ss = ss.source_set()
vfio_user_ss.add(files(
  'container.c',
  'device.c',
  'pci.c',
  'proxy.c',
))

if get_option('vfio_user_client').enabled()
  specific_ss.add_all(vfio_user_ss)
endif
