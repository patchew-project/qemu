system_ss.add(when: 'CONFIG_NVME_PCI',
              if_true: files('ctrl.c', 'dif.c', 'monitor.c', 'nguid.c', 'ns.c', 'subsys.c'),
              if_false: files('monitor-stub.c'))
