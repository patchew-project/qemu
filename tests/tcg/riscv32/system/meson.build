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
  'test-mseccfg-bits-1.S': {
    'cflags': cflags,
    'qemu_args': ['-cpu', 'rv32,u=off,h=off,s=off,zkr=on,zicfilp=off', qemu_args],
  },
  'test-mseccfg-bits-2.S': {
    'cflags': cflags,
    'qemu_args': ['-cpu', 'rv32,smepmp=true,zkr=false,zicfilp=false', qemu_args],
  },
  'test-mseccfg-bits-3.S': {
    'cflags': cflags,
    'qemu_args': ['-cpu', 'rv32,zkr=false,zicfilp=true', qemu_args],
  },
  'test-mseccfg-bits-4.S': {
    'cflags': cflags,
    'qemu_args': ['-cpu', 'rv32,zkr=true', qemu_args],
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
