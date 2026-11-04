# SPDX-License-Identifier: GPL-2.0-or-later
#
# Copyright (c) 2026 Process Mission

ppc64_system_cflags = ppc_system_cflags + ['-m64']
tests = []
foreach cpu: ['e5500', 'e6500']
  sources = ppc_system_sources
  foreach src: sources
    tests += {
      '../ppc/system/' + src: {
        'exe_name': fs.stem(src) + '-' + cpu + '-64',
        'cflags': ppc64_system_cflags,
        'qemu_args': ppc_system_qemu_args + ['-cpu', cpu, '-kernel'],
        'expected_output': '../ppc/system/isa.out',
      }
    }
  endforeach
endforeach

if 'qemu-system-ppc64' in emulators
  tcg_tests += {
    'ppc64-softmmu': {
      'cc': 'powerpc64-linux-gnu-gcc',
      'cc_dockerfile': 'debian-all-test-cross',
      'folder': 'ppc64',
      'gdb_arch': 'ppc64',
      'qemu': emulators['qemu-system-ppc64'],
      'tests': tests,
    }
  }
endif
