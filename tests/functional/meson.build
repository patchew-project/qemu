# QEMU functional tests:
# Tests that are put in the 'quick' category are run by default during
# 'make check'. Everything that should not be run during 'make check'
# (e.g. tests that fetch assets from the internet) should be put into
# the 'thorough' category instead.

# Most tests run too slow with TCI enabled, so skip the functional tests there
if get_option('tcg_interpreter')
  subdir_done()
endif

# Define the GDB runner if GDB is available.
gdb = get_option('gdb')
if gdb != ''
    gdb_runner_script =  meson.project_source_root() + '/tests/guest-debug/run-test.py'
    gdb_runner = [gdb_runner_script, '--quiet', '--gdb', gdb, '--test']

    message('GDB runner defined based on GDB found at: ', gdb)

    # A test with a runner is a tuple (name, runner, protocol).
    # The tests must be elements of an array named like:
    #
    # test_<arch>_<mode=[system|linuxuser|bsduser]>_<speed=[quick|thorough]>_with_runner = [
    #      ['test0', gdb_runner, 'exitcode'],
    #      ...
    # ]
else
    message('GDB not found, skipping functional tests that rely on it.')
endif

subdir('aarch64')
subdir('alpha')
subdir('arm')
subdir('avr')
subdir('hppa')
subdir('i386')
subdir('loongarch64')
subdir('m68k')
subdir('microblaze')
subdir('microblazeel')
subdir('mips')
subdir('mipsel')
subdir('mips64')
subdir('mips64el')
subdir('or1k')
subdir('ppc')
subdir('ppc64')
subdir('riscv32')
subdir('riscv64')
subdir('rx')
subdir('s390x')
subdir('sh4')
subdir('sh4eb')
subdir('sparc')
subdir('sparc64')
subdir('x86_64')
subdir('xtensa')
subdir('generic')

precache_all = []
foreach speed : ['quick', 'thorough']
  foreach dir : target_dirs

    target_base = dir.split('-')[0]

    if dir.endswith('-softmmu')
      sysmode = 'system'
      test_emulator = emulators['qemu-system-' + target_base]
    elif dir.endswith('-linux-user')
      sysmode = 'linuxuser'
      test_emulator = emulators['qemu-' + target_base]
    elif dir.endswith('-bsd-user')
      sysmode = 'bsduser'
      test_emulator = emulators['qemu-' + target_base]
    else
      continue
    endif

    if speed == 'quick'
      suites = ['func-quick', 'func-' + target_base]
      target_tests = get_variable('tests_' + target_base + '_' + sysmode + '_quick', []) \
                     + get_variable('tests_generic_' + sysmode)
      target_tests_r = get_variable('tests_' + target_base + '_' + sysmode + '_quick_with_runner', [])
    else
      suites = ['func-' + speed, 'func-' + target_base + '-' + speed, speed]
      target_tests = get_variable('tests_' + target_base + '_' + sysmode + '_' + speed, [])
      target_tests_r = get_variable('tests_' + target_base + '_' + sysmode + '_' + speed + '_with_runner', [])
    endif

    test_deps = [roms, keymap_targets]
    test_env = environment()
    if have_tools
      test_env.set('QEMU_TEST_QEMU_IMG', meson.global_build_root() / 'qemu-img')
      test_deps += [qemu_img]
    endif
    test_env.set('QEMU_TEST_QEMU_BINARY', test_emulator.full_path())
    test_env.set('QEMU_BUILD_ROOT', meson.project_build_root())
    test_env.set('PYTHONPATH', meson.project_source_root() / 'python:' +
                               meson.current_source_dir())

    foreach test : target_tests
      testname = '@0@-@1@'.format(target_base, test)
      if fs.exists('generic' / 'test_' + test + '.py')
        testfile = 'generic' / 'test_' + test + '.py'
      else
        testfile = target_base / 'test_' + test + '.py'
      endif
      testpath = meson.current_source_dir() / testfile
      teststamp = testname + '.tstamp'
      test_precache_env = environment()
      test_precache_env.set('QEMU_TEST_PRECACHE', meson.current_build_dir() / teststamp)
      test_precache_env.set('PYTHONPATH', meson.project_source_root() / 'python:' +
                                          meson.current_source_dir())
      precache = custom_target('func-precache-' + testname,
                               output: teststamp,
                               command: [python, testpath],
                               depend_files: files(testpath),
                               build_by_default: false,
                               env: test_precache_env)
      precache_all += precache
      if is_variable('test_' + target_base + '_timeouts')
        time_out = get_variable('test_' + target_base + '_timeouts').get(test, 90)
      else
        time_out = 90
      endif

      # Ideally we would add 'precache' to 'depends' here, such that
      # 'build_by_default: false' lets the pre-caching automatically
      # run immediately before the test runs. In practice this is
      # broken in meson, with it running the pre-caching in the normal
      # compile phase https://github.com/mesonbuild/meson/issues/2518
      # If the above bug ever gets fixed, when QEMU changes the min
      # meson version, add the 'depends' and remove the custom
      # 'run_target' logic below & in Makefile.include
      test('func-' + testname,
           python,
           depends: [test_deps, test_emulator, emulator_modules, plugin_modules],
           env: test_env,
           args: [testpath],
           protocol: 'tap',
           timeout: time_out,
           priority: time_out,
           suite: suites)
    endforeach

    # Prepare tests that require a specific runner.
    foreach test : target_tests_r
      testname = '@0@-@1@'.format(target_base, test[0])
      testfile = target_base / 'test_' + test[0] + '.py'
      testpath = meson.current_source_dir() / testfile
      teststamp = testname + '.tstamp'
      testrunner  = test[1]
      testproto = test[2]

      # python_site_packages, i.e., site packages from Python in pyvenv, is
      # added to PYTHONPATH because some runners can run a program that has its
      # own Python hooks that, by its turn, will search for modules based on
      # PYTHONPATH independently of the Python used by the runner, like, for
      # example, GDB using libpython.
      test_r_precache_env = test_precache_env
      test_r_precache_env.append('PYTHONPATH', python_site_packages)
      # For similar reasons, PYTHONPATH must also include the path to the test
      # scripts, otherwise unittest's introspection will failed.
      test_r_precache_env.append('PYTHONPATH', meson.current_source_dir() / target_base)

      precache = custom_target('func-precache-' + testname,
                               output: teststamp,
                               command: [testrunner, testpath],
                               depend_files: files(testpath),
                               build_by_default: false,
                               env: test_r_precache_env)
      precache_all += precache

      # See comments above about PYTHONPATH in test_r_precache_env.
      test_r_env = test_env
      test_r_env.append('PYTHONPATH', python_site_packages)
      test_r_env.append('PYTHONPATH', meson.current_source_dir() / target_base)

      test('func-' + testname,
           python,
           depends: [test_deps, test_emulator, emulator_modules, plugin_modules],
           env: test_r_env,
           args: [testrunner, testpath],
           protocol: testproto,
           timeout: time_out,
           priority: time_out,
           suite: suites)
    endforeach
  endforeach
endforeach

run_target('precache-functional',
           depends: precache_all,
           command: [python, '-c', ''])
