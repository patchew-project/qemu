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
  '../riscv/zl/zl.S': {
    'cflags': cflags + ['-march=rv32i_zicsr_zifencei'],
    'qemu_args': ['-M', 'virt', '-m', '16M', '-bios', 'none', '-display', 'none',
                  '-serial', 'none', '-monitor', 'none', '-semihosting',
                  '-cpu', 'rv32,x-zl=true,c=false,zca=false', '-kernel'],
  },
}

if 'qemu-system-riscv32' in emulators
  tcg_tests += {
    'riscv32-softmmu': {
      'cc': 'riscv64-linux-gnu-gcc',
      'cc_dockerfile': 'debian-all-test-cross',
      'folder': 'riscv32',
      'gdb_arch': 'riscv32',
      'qemu': emulators['qemu-system-riscv32'],
      'tests': tests,
    }
  }
endif
