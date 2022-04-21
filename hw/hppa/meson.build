hppa_ss = ss.source_set()
hppa_ss.add(when: 'CONFIG_HPPA_GENERIC', if_true: files('pci.c', 'machine.c'))

hw_arch += {'hppa': hppa_ss}
