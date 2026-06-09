cc = find_program('aarch64-linux-gnu-gcc', required : false)

if not cc.found()
  subdir_done()
endif

feat_cflags = {
  'aes': '-march=armv8-a+aes',
  'sve': '-march=armv8.1-a+sve',
  'sve2': '-march=armv8.1-a+sve2',
  'v8.2': '-march=armv8.2-a',
  'v8.3': '-march=armv8.3-a',
  'v8.5': '-march=armv8.5-a',
  'bti':  '-mbranch-protection=standard',
  'mte':  '-march=armv8.5-a+memtag',
  'sme':  '-Wa,-march=armv9-a+sme',
}
cc_has_feat = {}

foreach feat, flags : feat_cflags
  cmd = run_command([cc, cc_check_args, flags], check: false)
  avail = cmd.returncode() == 0
  cc_has_feat += {feat: avail}
  if not avail
    warning('aarch64 tcg tests for feature \'' + feat + '\' ' +
            'disabled: unsupported by cross-compiler')
  endif
endforeach

gdb_feat_version = {
  'sme-tiles': '14.1',
  'mte': '15.1',
  'mte-baremetal': '16.0',
}
gdb_has_feat = {}

foreach feat, min_ver : gdb_feat_version
  avail = gdb_version.version_compare('>=' + min_ver)
  gdb_has_feat += {feat: avail}
  if not avail
    warning('aarch64 gdb tests for feature \'' + feat + '\' ' +
            'disabled: unsupported by gdb')
  endif
endforeach

subdir('system')

if 'qemu-aarch64' not in emulators
  subdir_done()
endif

tests = []

# Base architecture tests
tests += {
  '../arm/fcvt.c': {'cflags': '-lm', 'expected_output': 'fcvt.ref'},
  'pcalign-a64.c': {},
  'lse2-fault.c': {},
  'test-2248.c': {},
  'test-2150.c': {},
}

if cc_has_feat['v8.2']
  tests += {'dcpop.c': {'cflags': feat_cflags['v8.2']}}
endif

if cc_has_feat['v8.5']
  tests += {'dcpodp.c': {'cflags': feat_cflags['v8.5']}}
endif

# Pauth Tests
if cc_has_feat['v8.3']
  f = feat_cflags['v8.3']
  cpu_max = ['-cpu', 'max']
  cpu_neoverse = ['-cpu', 'neoverse-v1']
  tests += {
    'test-2375.c': {'cflags': f},
    'pauth-1.c': {'cflags': f, 'qemu_args': cpu_max},
    'pauth-2.c': {'cflags': f, 'qemu_args': cpu_max},
    # Choose a cpu with FEAT_Pauth but without FEAT_FPAC for pauth-[45].
    'pauth-4.c': {'cflags': f, 'qemu_args': cpu_neoverse},
    'pauth-5.c': {'cflags': f, 'qemu_args': cpu_neoverse},
  }
endif

# BTI Tests
# bti-1 tests the elf notes, so we require special compiler support.
if cc_has_feat['bti']
  f = [feat_cflags['bti'], '-fno-stack-protector', '-nostdlib']
  tests += {
    'bti-1.c': {'cflags': f},
    'bti-3.c': {'cflags': f},
  }
endif
# bti-2 tests PROT_BTI, so no special compiler support required.
tests += {'bti-2.c': {}}

# MTE Tests
if cc_has_feat['mte']
  f = feat_cflags['mte']
  tests += {
    'mte-1.c': {'cflags': f},
    'mte-2.c': {'cflags': f},
    'mte-3.c': {'cflags': f},
    'mte-4.c': {'cflags': f},
    'mte-5.c': {'cflags': f},
    'mte-6.c': {'cflags': f},
    'mte-7.c': {'cflags': f},
    'mte-8.c': {'cflags': f},
  }
endif

# SME Tests
if cc_has_feat['sme']
  f = feat_cflags['sme']
  tests += {
    'sme-outprod1.c': {'cflags': f},
    'sme-smopa-1.c': {'cflags': f},
    'sme-smopa-2.c': {'cflags': f},
    'sme-fmopa-1.c': {'cflags': f},
    'sme-fmopa-2.c': {'cflags': f},
    'sme-fmopa-3.c': {'cflags': f},
  }
endif

# GCS Tests
tests += {
  'gcsstr.c': {},
  'gcspushm.c': {},
  'gcsss.c': {},
}

# System Registers Tests
tests += {'sysregs.c': {}}

# Crypto Tests
tests += {'test-aes.c': {'cflags': feat_cflags['aes']}}

# SVE Tests
if cc_has_feat['sve']
  f = feat_cflags['sve']
  tests += {
    'sve-ioctls.c': {'cflags': f},
    'sve-str.c': {'cflags': [f, '-O1']},
  }
endif

# SVE2 Tests
if cc_has_feat['sve2']
  tests += {'test-826.c': {'cflags': feat_cflags['sve2']}}
endif

# GDB Tests
if cc_has_feat['sve']
  f = feat_cflags['sve']
  tests += {
    'sysregs.c': {
      'exe_name': 'sysregs-sve',
      'cflags': f,
      'gdb_test': ['--test', files('gdbstub/test-sve.py')],
    },
    'sve-ioctls.c': {
      'gdb_test': ['--test', files('gdbstub/test-sve-ioctl.py')],
    },
  }
endif

if cc_has_feat['sme']
  f = feat_cflags['sme']
  sysregs_sme = 'sysregs-sme'
  tests += {
    'sysregs.c': {
      'exe_name': sysregs_sme,
      'cflags': f,
      'gdb_test': ['--test', files('gdbstub/test-sme.py'), '--',
                   'test_sme', '--gdb_basic_za_test'],
    }
  }
  if gdb_has_feat['sme-tiles']
    tests += {
      'sysregs.c': {
        'test_name': 'sysregs-sme-tile-slice',
        'exe_name': sysregs_sme,
        'gdb_test': ['--test', files('gdbstub/test-sme.py'), '--',
                     'test_sme', '--gdb_tile_slice_test'],
      }
    }
  endif
  tests += {
    'sysregs.c': {
      'test_name': 'sysregs-sme2',
      'exe_name': sysregs_sme,
      'gdb_test': ['--test', files('gdbstub/test-sme2.py')],
    }
  }
endif

if cc_has_feat['mte'] and gdb_has_feat['mte']
  f = feat_cflags['mte']
  tests += {
    'mte-8.c': {
      'gdb_test': ['--test', files('gdbstub/test-mte.py'), '--', '--mode=user'],
    }
  }
endif

tcg_tests += {
  'aarch64-linux-user': {
    'cc': cc,
    'folder': 'aarch64',
    'gdb_arch': 'aarch64',
    'qemu': emulators['qemu-aarch64'],
    'tests': tests,
  }
}
