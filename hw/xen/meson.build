system_ss.add(when: ['CONFIG_XEN_BUS'], if_true: files(
  'xen-backend.c',
  'xen-bus-helper.c',
  'xen-bus.c',
  'xen-legacy-backend.c',
  'xen_devconfig.c',
  'xen_pvdev.c',
))

system_ss.add(when: ['CONFIG_XEN'], if_true: files(
  'xen-operations.c',
  'xen-mapcache.c',
))
system_ss.add(when: ['CONFIG_XEN', xen], if_true: files(
  'xen-hvm-common.c',
))

if have_xen_pci_passthrough
  system_ss.add(when: ['CONFIG_XEN'], if_true: files(
    'xen-host-pci-device.c',
    'xen_pt_config_init.c',
    'xen_pt_load_rom.c',
  ))
  system_ss.add(when: ['CONFIG_XEN', xen], if_true: files(
    'xen_pt.c',
    'xen_pt_graphics.c',
    'xen_pt_msi.c',
  ))
else
  system_ss.add(when: ['CONFIG_XEN'], if_true: files('xen_pt_stub.c'))
endif
