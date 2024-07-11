slow_pytests = {
  'mem_addr_space' : 90,
}

pytests_generic = [
  'empty_cpu_model',
  'info_usernet',
  'version',
]

pytests_arm = [
  'machine_arm_n8x0',
]

pytests_avr = [
  'machine_avr6',
]

pytests_loongarch64 = [
  'machine_loongarch',
]

pytests_mips64el = [
  'machine_mips_loongson3v',
]

pytests_x86_64 = [
  'cpu_queries',
  'mem_addr_space',
  'virtio_version',
]

pytest = find_program('pytest', required: false)
if not pytest.found()
  message('pytest not available ==> Disabled the qemu-pytests.')
  subdir_done()
endif

foreach dir : target_dirs
  if not dir.endswith('-softmmu')
    continue
  endif

  target_base = dir.split('-')[0]
  pytest_emulator = emulators['qemu-system-' + target_base]
  target_pytests = get_variable('pytests_' + target_base, []) + pytests_generic

  test_deps = roms
  pytest_env = environment()
  if have_tools
    pytest_env.set('PYTEST_QEMU_IMG', './qemu-img')
    test_deps += [qemu_img]
  endif
  pytest_env.set('PYTEST_QEMU_BINARY', meson.global_build_root() / 'qemu-system-' + target_base)
  pytest_env.set('PYTEST_SOURCE_ROOT', meson.project_source_root())
  pytest_env.set('PYTEST_BUILD_ROOT', meson.project_build_root())
  pytest_env.set('PYTHONPATH', meson.project_source_root() / 'python')

  foreach test : target_pytests
    test('pytest-@0@/@1@'.format(target_base, test),
         pytest,
         depends: [test_deps, pytest_emulator, emulator_modules],
         env: pytest_env,
         args: [meson.current_source_dir() / 'test_' + test + '.py'],
         timeout: slow_pytests.get(test, 60),
         priority: slow_pytests.get(test, 60),
         suite: ['pytest', 'pytest-' + target_base])
  endforeach
endforeach
