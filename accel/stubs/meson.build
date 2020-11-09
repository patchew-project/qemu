accel_stubs_ss = ss.source_set()

accel_stubs_ss.add(when: 'CONFIG_HAX', if_false: files('hax-stub.c'))
accel_stubs_ss.add(when: 'CONFIG_XEN', if_false: files('xen-stub.c'))
accel_stubs_ss.add(when: 'CONFIG_KVM', if_false: files('kvm-stub.c'))
accel_stubs_ss.add(when: 'CONFIG_TCG', if_false: files('tcg-stub.c'))

specific_ss.add_all(when: ['CONFIG_SOFTMMU'], if_true: accel_stubs_ss)
