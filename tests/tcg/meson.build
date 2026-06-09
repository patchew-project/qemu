env = find_program('env')

tcg_tests = {}
# tcg_tests is a dictionary with following structure:
#
# {
#   'name_of_target': {
#     'cc': cross_compiler,
#     'folder': 'folder_for_test_src_files',
#     'qemu': emulators['qemu-...'],
#     'tests': [
#       {
#         'src_file': {
#           'exe_name': ['provide an alternative binary name'],
#           'test_name': ['provide an alternative test name'],
#         }
#       },
#       ...
#     ]
#   }
# }
#
# Every test executable, identified by 'exe_name' is built only once.
# Tests for a given src use the same executable by default, and their definition
# is guaranteed to be unique also. They can be duplicated by setting 'test_name'.
# Default name is derived from src if 'exe_name' and 'test_name' are omitted.

# plugins come first, as we need to build the list
subdir('plugins')

# Finally, we can create all test executables and test targets
foreach target, plan: tcg_tests
  # Detect duplicated executables/tests, and report an error to force user to
  # choose how to deal with it.
  built_tests = {}
  added_tests = {}

  # return a clear error if user mispell a target entry
  foreach key, _ : plan
    if key not in ['cc', 'folder', 'qemu', 'tests']
      error('unknown tcg test plan entry \'' + key + '\' for target ' + target)
    endif
  endforeach

  cc = plan['cc']
  folder = plan['folder']
  qemu = plan['qemu']
  tests = plan['tests']

  foreach t : tests
    foreach src, setup: t
      # return a clear error if user mispell a setup entry
      foreach key, _ : setup
        if key not in [
                       'exe_name', 'test_name',
                       ]
          error('unknown tcg setup entry \'' + key + '\' for test ' + src)
        endif
      endforeach

      # meson '/' operator drops left operand if right is an absolute path
      src = folder / src
      file = files(src)
      test = fs.name(file)
      exe_name = fs.stem(src)
      if 'exe_name' in setup
        exe_name = setup['exe_name']
      endif

      test_name = exe_name
      if 'test_name' in setup
        test_name = setup['test_name']
      endif

      exe_name = target + '-' + exe_name
      test_name = target + '-' + test_name

      if test_name in added_tests
        error('test ' + test_name + ' was already added: ' +
              'specify a different \'test_name\'')
      endif

      # build executable if needed
      if exe_name not in built_tests
        exe = custom_target(exe_name,
                            input: file,
                            command: [cc, '@INPUT@',
                                      '-o', '@OUTPUT@', '-static',
                                      '-MMD', '-MF', '@DEPFILE@',
                                      '-Wall', '-Werror',
                                      '-O0', '-g', '-fno-strict-aliasing',
                                      ],
                            depfile: exe_name + '.d',
                            output: exe_name + '.test',
                            build_by_default: false)
        built_tests += {exe_name: exe}
      endif

      depends = []

      exe = built_tests[exe_name]
      cmd = [qemu, exe]

      # some tests expect QEMU env var to be set
      cmd = ['QEMU=' + qemu.full_path(), cmd]

      test(test_name, env, args: cmd,
           depends: depends,
           suite: ['tcg', 'tcg-' + target])
      added_tests += {test_name: true}
    endforeach
  endforeach
endforeach
