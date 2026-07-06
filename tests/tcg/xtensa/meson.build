tests = []

cc = 'xtensa-dc232b-elf-gcc'
core = 'dc232b'

core_dir = meson.source_root() / 'target' / 'xtensa' / 'core-' + core
link_script_src = files('linker.ld.S')[0]
link_script = custom_target('xtensa_linker.ld',
  command: [meson.get_compiler('c').cmd_array(),
            '-E', '-P', '-I', core_dir,
            '@INPUT@',
            '-o', '@OUTPUT@'],
  input: link_script_src,
  output: 'xtensa_linker.ld',
)
crt = files('crt.S')[0]
vectors = files('vectors.S')[0]
cflags = ['-nostdlib',
          '-nostartfiles',
          '-mtext-section-literals',
          crt, vectors,
          '-I', core_dir,
          '-Wa,--no-absolute-literals',
          '-Wl,-T', link_script,
          '-lgcc']
qemu_args = ['-M', 'sim', '-cpu', core,
             '-display', 'none',
             '-semihosting', '-icount', '6',
             '-kernel']
setup = {'cflags': cflags, 'qemu_args': qemu_args}

# Multi arch tests are not supported
# tests += tcg_tests['multiarch-softmmu']['tests']

tests += {
  'test_b.S': setup,
  'test_bi.S': setup,
  'test_boolean.S': setup,
  'test_break.S': setup,
  'test_bz.S': setup,
  'test_cache.S': setup,
  'test_clamps.S': setup,
  'test_dfp0_arith.S': setup,
  'test_exclusive.S': setup,
  'test_extui.S': setup,
  'test_flix.S': setup,
  'test_fp0_arith.S': setup,
  'test_fp0_conv.S': setup,
  'test_fp0_div.S': setup,
  'test_fp0_sqrt.S': setup,
  'test_fp1.S': setup,
  'test_fp_cpenable.S': setup,
  'test_interrupt.S': setup,
  'test_load_store.S': setup,
  'test_loop.S': setup,
  'test_lsc.S': setup,
  'test_mac16.S': setup,
  'test_max.S': setup,
  'test_min.S': setup,
  'test_mmu.S': setup,
  'test_mul16.S': setup,
  'test_mul32.S': setup,
  'test_nsa.S': setup,
  'test_phys_mem.S': setup,
  'test_quo.S': setup,
  'test_rem.S': setup,
  'test_rst0.S': setup,
  'test_s32c1i.S': setup,
  'test_sar.S': setup,
  'test_sext.S': setup,
  'test_shift.S': setup,
  'test_sr.S': setup,
  'test_timer.S': setup,
  'test_windowed.S': setup,
}

if 'qemu-system-xtensa' in emulators
  tcg_tests += {
    'xtensa-softmmu': {
      'cc': cc,
      'cc_dockerfile': 'debian-xtensa-cross',
      'cc_docker_arch': ['x86_64'],
      'folder': 'xtensa',
      'gdb_arch': 'xtensa',
      'qemu': emulators['qemu-system-xtensa'],
      'tests': tests,
    }
  }
endif
