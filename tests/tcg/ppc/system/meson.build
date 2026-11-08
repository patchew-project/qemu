# SPDX-License-Identifier: GPL-2.0-or-later
#
# Copyright (c) 2026 Process Mission

# Shared settings and sources for the ppc and ppc64 system targets
ppc_system_link_script = files('kernel.ld')[0]
ppc_system_boot = files('boot.S')[0]
ppc_system_cflags = ['-nostdlib', '-ffreestanding', '-mcpu=power6',
                     '-Wa,-mregnames', '-Wa,--noexecstack',
                     '-Wl,--build-id=none',
                     '-Wl,-T', ppc_system_link_script, ppc_system_boot]
ppc_system_qemu_args = ['-M', 'ppce500', '-accel', 'tcg', '-display', 'none',
                        '-serial', 'stdio', '-monitor', 'none', '-nic', 'none']
ppc_system_sources = ['unsupported.S']

tests = []
foreach src: ppc_system_sources
  tests += {
    'system/' + src: {
      'exe_name': fs.stem(src) + '-e500mc-32',
      'cflags': ppc_system_cflags + ['-m32', '-DE500MC'],
      'qemu_args': ppc_system_qemu_args + ['-cpu', 'e500mc', '-kernel'],
      'expected_output': 'system/isa.out',
    }
  }
endforeach

if 'qemu-system-ppc' in emulators
  tcg_tests += {
    'ppc-softmmu': {
      'cc': 'powerpc-linux-gnu-gcc',
      'cc_dockerfile': 'debian-all-test-cross',
      'folder': 'ppc',
      'gdb_arch': 'ppc',
      'qemu': emulators['qemu-system-ppc'],
      'tests': tests,
    }
  }
endif
