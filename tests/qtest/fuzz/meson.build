specific_fuzz_ss.add(files('fuzz.c', 'fork_fuzz.c', 'qos_fuzz.c'), qos)

# Targets
specific_fuzz_ss.add(when: 'CONFIG_I440FX', if_true: files('i440fx_fuzz.c'))
specific_fuzz_ss.add(when: 'CONFIG_VIRTIO_NET', if_true: files('virtio_net_fuzz.c'))
specific_fuzz_ss.add(when: 'CONFIG_VIRTIO_SCSI', if_true: files('virtio_scsi_fuzz.c'))

# unfortunately declare_dependency does not support link_depends, so
# this will be duplicated in meson.build
fork_fuzz = declare_dependency(
  link_args: ['-fsanitize=fuzzer',
              '-Ttests/qtest/fuzz/fork_fuzz.ld',
              '-wrap', 'qtest_inb',
              '-wrap', 'qtest_inw',
              '-wrap', 'qtest_inl',
              '-wrap', 'qtest_outb',
              '-wrap', 'qtest_outw',
              '-wrap', 'qtest_outl',
              '-wrap', 'qtest_readb',
              '-wrap', 'qtest_readw',
              '-wrap', 'qtest_readl',
              '-wrap', 'qtest_readq',
              '-wrap', 'qtest_writeb',
              '-wrap', 'qtest_writew',
              '-wrap', 'qtest_writel',
              '-wrap', 'qtest_writeq',
              '-wrap', 'qtest_memread',
              '-wrap', 'qtest_bufread',
              '-wrap', 'qtest_memwrite',
              '-wrap', 'qtest_bufwrite',
              '-wrap', 'qtest_memset']
)

specific_fuzz_ss.add(fork_fuzz)
