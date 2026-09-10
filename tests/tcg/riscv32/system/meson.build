# SPDX-License-Identifier: GPL-2.0-or-later

tests = []

link_script = files('../semihost.ld')[0]
cflags = ['-march=rv32im_zicsr',
          '-mabi=ilp32',
          '-nostdlib',
          '-ffreestanding',
          '-Wa,--noexecstack',
          '-Wl,-T', link_script]
qemu_args = ['-M', 'virt',
             '-display', 'none',
             '-serial', 'stdio',
             '-semihosting',
             '-bios']

tests += {
  'test-mcycle.S': {
    'cflags': cflags,
    'qemu_args': ['-icount', 'shift=1', qemu_args],
  },
}

tests += {
  'smcdeleg-minh-rv32.S': {
    'cflags': cflags,
    'qemu_args': ['-cpu', 'max', qemu_args],
  },
}

tests += {
  'pmu-minstretcfg-rv32.S': {
    'cflags': cflags,
    'qemu_args': ['-cpu', 'max', qemu_args],
  },
}

tests += {
  'smcdeleg-rv32.S': {
    'cflags': cflags,
    'qemu_args': ['-cpu', 'max', qemu_args],
  },
}

if 'qemu-system-riscv32' in emulators
  tcg_tests += {
    'riscv32-softmmu': {
      'cc': cc,
      'cc_dockerfile': cc_dockerfile,
      'cc_docker_host_arch': cc_docker_host_arch,
      'folder': 'riscv32',
      'gdb_arch': 'riscv32',
      'qemu': emulators['qemu-system-riscv32'],
      'tests': tests,
    }
  }
endif
