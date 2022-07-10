slow_pytests = {
  'acpi-bits' : 120,
}

pytests_generic = []

# biosbits tests are currenly only supported on x86_64 platforms.
pytests_x86_64 = ['acpi-bits-test']

pytest_executables = {}
other_deps = []

subdir('acpi-bits')

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
    pytest_env.set('QTEST_QEMU_IMG', './qemu-img')
    test_deps += [qemu_img]
  endif
  pytest_env.set('G_TEST_DBUS_DAEMON', meson.project_source_root() / 'tests/dbus-vmstate-daemon.sh')
  pytest_env.set('PYTEST_QEMU_BINARY', './qemu-system-' + target_base)
  pytest_env.set('PYTEST_SOURCE_ROOT', meson.project_source_root())
  if have_tools and have_vhost_user_blk_server
    pytest_env.set('PYTEST_QEMU_STORAGE_DAEMON_BINARY', './storage-daemon/qemu-storage-daemon')
    test_deps += [qsd]
  endif

  foreach test : target_pytests
    test('pytest-@0@/@1@'.format(target_base, test),
         pytest_executables[test],
         depends: [test_deps, pytest_emulator, emulator_modules, other_deps],
         env: pytest_env,
         args: ['--tap', '-k'],
         protocol: 'tap',
         timeout: slow_pytests.get(test, 30),
         priority: slow_pytests.get(test, 30),
         suite: ['pytest', 'pytest-' + target_base])
  endforeach
endforeach
