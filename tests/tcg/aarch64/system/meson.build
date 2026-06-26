if 'qemu-system-aarch64' not in emulators
  subdir_done()
endif

tests = []

minilib_dir = meson.current_source_dir() / '..' / '..' / 'minilib'
minilib_printf = files(minilib_dir / 'printf.c')[0]
link_script = files('kernel.ld')[0]
boot = files('boot.S')[0]
cflags = ['-nostdlib',
          '-Wa,--noexecstack',
          '-I', minilib_dir, minilib_printf, boot,
          '-Wl,-T', link_script]
qemu_base_args = ['-display', 'none',
                  '-semihosting-config', 'enable=on',
                  '-kernel']
qemu_def_args = ['-M', 'virt', '-cpu', 'max', qemu_base_args]

# Multi arch tests - add cflags only once per src
multi_src = []
foreach t: tcg_tests['multiarch-softmmu']['tests']
  foreach src, setup: t
    new_cflags = cflags
    if fs.stem(src) == 'memory'
      new_cflags += '-DCHECK_UNALIGNED=1'
    endif
    if src not in multi_src
      setup += {'cflags': [new_cflags]}
      multi_src += src
    endif
    tests += {src: setup + {'qemu_args': qemu_def_args}}
  endforeach
endforeach

if cc_has_feat['sve']
  tests += {
    '../../multiarch/system/memory.c': {
      'exe_name': 'memory-sve',
      'cflags': [cflags, feat_cflags['sve'], '-DCHECK_UNALIGNED=1', '-O3'],
      'qemu_args': qemu_def_args,
    },
  }
endif

tests += {
  'asid2.c': {'cflags': cflags, 'qemu_args': qemu_def_args},
  'feat-xs.c': {'cflags': cflags, 'qemu_args': qemu_def_args},
  'rme_gdi.c': {'cflags': cflags, 'qemu_args': qemu_def_args},
  'semiheap.c': {'cflags': cflags, 'qemu_args': qemu_def_args},
  'semiconsole.c': {
    'cflags': cflags,
    'qemu_args': ['-serial', 'none', '-chardev', 'stdio,mux=on,id=stdio0',
                  '-semihosting-config', 'enable=on,chardev=stdio0',
                  '-mon', 'chardev=stdio0,mode=readline',
                  qemu_def_args],
    'wrapper': [prog_run_with_input, 'X']
  },
  '../../multiarch/system/memory.c': {
    'test_name': 'memory-record-replay',
    'qemu_args': qemu_def_args,
    'wrapper': [prog_record_replay],
  },
  'vtimer.c': {
    'cflags': cflags,
    'qemu_args': ['-M', 'virt,virtualization=on,gic-version=2',
                  '-cpu', 'cortex-a57', '-smp', '4',
                  '-semihosting-config', 'enable=on,arg=2',
                  qemu_base_args]
  },
}

if cc_has_feat['v8.3']
  tests += {
    'pauth-3.c': {
      'cflags': [feat_cflags['v8.3'], cflags],
      'qemu_args': ['-M', 'virt', '-cpu', 'max,pauth-qarma5=on', qemu_base_args],
    }
  }
endif

if cc_has_feat['mte'] and gdb_has_feat['mte-baremetal']
  tests += {
    'mte.S': {
      'cflags': [feat_cflags['mte'], cflags],
      'qemu_args': ['-M', 'virt,mte=on', '-cpu', 'max', qemu_base_args],
      'gdb_test': ['--test', files('../gdbstub/test-mte.py'), '--', '--mode=system'],
    }
  }
endif

tcg_tests += {
  'aarch64-softmmu': {
    'cc': cc,
    'folder': 'aarch64/system',
    'gdb_arch': 'aarch64',
    'qemu': emulators['qemu-system-aarch64'],
    'tests': tests,
  }
}
