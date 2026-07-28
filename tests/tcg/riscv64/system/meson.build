tests = []

# Multi arch tests are not supported
# tests += tcg_tests['multiarch-softmmu']['tests']

link_script = files('../semihost.ld')[0]
cflags = ['-nostdlib',
          '-Wa,--noexecstack',
          '-Wl,-T', link_script]
qemu_args = ['-M', 'virt',
             '-display', 'none',
             '-serial', 'stdio',
             '-semihosting',
             '-bios']

setup = {'cflags': cflags, 'qemu_args': qemu_args}

tests += {
  'doubletrap.S': setup + {'plugin_test': {'plugin': 'discons'}},
  'interruptedmemory.S': setup + {'plugin_test': {'plugin': 'discons'}},
  'issue1060.S': setup,
  'test-mepc-masking.S': setup,
  'test-crc32.S': setup + {'qemu_args': ['-cpu', 'rv64,xlrbr=true', qemu_args]},
}

if 'qemu-system-riscv64' in emulators
  tcg_tests += {
    'riscv64-softmmu': {
      'cc': cc,
      'cc_dockerfile': cc_dockerfile,
      'cc_docker_host_arch': cc_docker_host_arch,
      'folder': 'riscv64',
      'gdb_arch': 'riscv64',
      'qemu': emulators['qemu-system-riscv64'],
      'tests': tests,
    }
  }
endif
