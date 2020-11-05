softmmu_stub_ss = ss.source_set()

softmmu_stub_ss.add(when: 'CONFIG_HAX', if_false: files('hax-stub.c'))
softmmu_stub_ss.add(when: 'CONFIG_XEN', if_false: files('xen-all-stub.c'))
softmmu_stub_ss.add(when: 'CONFIG_XEN_HVM', if_false: files('xen-stub.c'))
softmmu_stub_ss.add(when: 'CONFIG_KVM', if_false: files('kvm-stub.c'))
softmmu_stub_ss.add(when: 'CONFIG_TCG', if_false: files('tcg-stub.c'))

specific_ss.add_all(when: 'CONFIG_SOFTMMU', if_true: softmmu_stub_ss)
