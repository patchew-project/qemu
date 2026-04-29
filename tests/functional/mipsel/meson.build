# SPDX-License-Identifier: GPL-2.0-or-later

test_mipsel_timeouts = {
  'malta' : 420,
  'pic32mk' : 30,
  'replay' : 480,
}

# Build the minimal PIC32MK test firmware from assembly source.
# Requires the mipsel-linux-gnu cross toolchain; silently skipped if absent.
_mipsel_cc = find_program('mipsel-linux-gnu-gcc', required: false)
_mipsel_objcopy = find_program('mipsel-linux-gnu-objcopy', required: false)

if _mipsel_cc.found() and _mipsel_objcopy.found() and \
   config_all_devices.has_key('CONFIG_PIC32MK')
  _fw_ld   = meson.current_source_dir() / 'pic32mk_test_fw.ld'
  _fw_src  = meson.current_source_dir() / 'pic32mk_test_fw.S'
  _fw_elf  = custom_target('pic32mk_test_fw_elf',
    input  : [_fw_src, _fw_ld],
    output : 'pic32mk_test_fw.elf',
    command: [_mipsel_cc,
              '-march=mips32r2', '-EL', '-nostdlib',
              '-T', _fw_ld, _fw_src,
              '-o', '@OUTPUT@'],
  )
  _fw_bin = custom_target('pic32mk_test_fw.bin',
    input  : _fw_elf,
    output : 'pic32mk_test_fw.bin',
    command: [_mipsel_objcopy, '-O', 'binary', '@INPUT@', '@OUTPUT@'],
  )
  tests_mipsel_system_quick = ['pic32mk']
else
  tests_mipsel_system_quick = []
endif

tests_mipsel_system_thorough = [
  'malta',
  'replay',
  'tuxrun',
]
