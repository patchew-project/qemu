tests = []

minilib_dir = meson.current_source_dir() / '..' / '..' / 'minilib'
minilib_printf = files(minilib_dir / 'printf.c')[0]
link_script = files('kernel.ld')[0]
boot = files('boot.S')[0]
cflags = ['-nostdlib',
          '-ffreestanding',
          '-Wa,--noexecstack',
          '-I', minilib_dir, minilib_printf, boot,
          '-Wl,-T', link_script,
          '-Wl,-melf_x86_64',
          '-Wl,--no-warn-rwx-segments',
          '-Wl,--build-id=none',
          '-lgcc']
qemu_def_args = ['-display', 'none',
                 '-device', 'isa-debugcon,chardev=stdio',
                 '-chardev', 'stdio,id=stdio',
                 '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x4',
                 '-kernel']

# Multi arch tests
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

tests += {
  'patch-target.c': {
    'cflags': cflags,
    'qemu_args': qemu_def_args,
    'plugin_test': {
      'plugin': 'patch',
      'args': ['target=ffc0', 'patch=9090', 'use_hwaddr=true'],
    },
    'wrapper': [prog_check_plugin_output,
                find_program('./validate-patch.py')]
  }
}

# The LAM test takes exceptions on the current stack, without CR4.OSFXSR.
lam_cflags = cflags + ['-mno-red-zone', '-mgeneral-regs-only']
foreach paging, extra: {'': [], '-la57': ['-DLA57']}
  foreach suffix, cpu: {'': 'max', '-off': 'max,lam=off'}
    tests += {
      'lam.c': {
        'exe_name': 'lam' + paging + suffix,
        'cflags': lam_cflags + extra +
                  ['-DEXPECT_LAM=' + (suffix == '' ? '1' : '0')],
        'qemu_args': ['-cpu', cpu] + qemu_def_args,
      }
    }
  endforeach
endforeach

if 'qemu-system-x86_64' in emulators
  tcg_tests += {
    'x86_64-softmmu': {
      'cc': 'x86_64-linux-gnu-gcc',
      'cc_alt': ['x86_64-redhat-linux-gcc', 'x86_64-elf-gcc'],
      'cc_dockerfile': 'debian-amd64-cross',
      'folder': 'x86_64/system',
      'gdb_arch': 'x86_64',
      'qemu': emulators['qemu-system-x86_64'],
      'tests': tests,
    }
  }
endif
