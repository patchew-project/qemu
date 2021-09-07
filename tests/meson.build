py3 = import('python').find_installation()

subdir('bench')

test_qapi_outputs = [
  'qapi-builtin-types.c',
  'qapi-builtin-types.h',
  'qapi-builtin-visit.c',
  'qapi-builtin-visit.h',
  'test-qapi-commands-sub-sub-module.c',
  'test-qapi-commands-sub-sub-module.h',
  'test-qapi-commands.c',
  'test-qapi-commands.h',
  'test-qapi-emit-events.c',
  'test-qapi-emit-events.h',
  'test-qapi-events-sub-sub-module.c',
  'test-qapi-events-sub-sub-module.h',
  'test-qapi-events.c',
  'test-qapi-events.h',
  'test-qapi-init-commands.c',
  'test-qapi-init-commands.h',
  'test-qapi-introspect.c',
  'test-qapi-introspect.h',
  'test-qapi-types-sub-sub-module.c',
  'test-qapi-types-sub-sub-module.h',
  'test-qapi-types.c',
  'test-qapi-types.h',
  'test-qapi-visit-sub-sub-module.c',
  'test-qapi-visit-sub-sub-module.h',
  'test-qapi-visit.c',
  'test-qapi-visit.h',
]

test_qapi_inputs = files(
  'qapi-schema/qapi-schema-test.json',
  'qapi-schema/include/sub-module.json',
  'qapi-schema/sub-sub-module.json'
)

test_qapi_files = custom_target('Test QAPI files',
                                output: test_qapi_outputs,
                                input: test_qapi_inputs,
                                command: [ qapi_gen, '-o', meson.current_build_dir(),
                                           '-b', '-p', 'test-', '@INPUT0@' ],
                                depend_files: qapi_gen_depends)

if with_rust
  test_qapi_rs_outputs = ['test-qapi-ffi-types.rs']
  test_qapi_rs = custom_target('Test QAPI Rust binding',
                               output: test_qapi_rs_outputs,
                               input: test_qapi_inputs,
                               command: [ qapi_gen, '-o', meson.current_build_dir(),
                                          '-r', '-b', '-p', 'test-', '@INPUT0@' ],
                               depend_files: qapi_gen_depends)
  qapi_cabi_rs = custom_target('qapi-cabi-rs',
                               output: ['qapi-cabi-rs' + exe_suffix],
                               build_always_stale: true,
                               depends: [test_qapi_rs],
                               command: [cargo_wrapper,
                                         meson.current_build_dir(),
                                         meson.current_source_dir(),
                                         meson.build_root(),
                                         rs_build_type,
                                         rust_target_triple,
                                         'build-bin',
                                         'qapi-cabi-rs',
                                         '--', '--cfg', 'QAPI_CABI'])
endif

# meson doesn't like generated output in other directories
# perhaps change qapi_gen to replace / with _, like Meson itself does?
subdir('include')

test_qapi_sources = []
test_qapi_headers = []
i = 0
foreach o: test_qapi_files.to_list() + test_qapi_files_extra.to_list()
  if o.full_path().endswith('.h')
    test_qapi_headers += o
  endif
  test_qapi_sources += o
endforeach

libtestqapi = static_library('testqapi',
                             sources: [genh, test_qapi_sources],
                             c_args: ['-DQAPI_CABI'])
testqapi = declare_dependency(link_with: libtestqapi,
                              dependencies: [qemuutil],
                              sources: [genh, test_qapi_headers])

qapi_cabi = executable('qapi-cabi',
                       files('qapi-cabi.c'),
                       dependencies: testqapi,
                       c_args: ['-DQAPI_CABI'])

if with_rust
  test('Test QAPI CABI diff',
       diff_commands,
       args: [qapi_cabi.full_path(), qapi_cabi_rs.full_path()],
       depends: [qapi_cabi, qapi_cabi_rs],
       suite: ['qapi'])
endif

test_deps = {
  'test-qht-par': qht_bench,
}

if have_tools and 'CONFIG_VHOST_USER' in config_host and 'CONFIG_LINUX' in config_host
  executable('vhost-user-bridge',
             sources: files('vhost-user-bridge.c'),
             dependencies: [qemuutil, vhost_user])
endif

if have_system and 'CONFIG_POSIX' in config_host
  subdir('qemu-iotests')
endif

test('decodetree', sh,
     args: [ files('decode/check.sh'), config_host['PYTHON'], files('../scripts/decodetree.py') ],
     workdir: meson.current_source_dir() / 'decode',
     suite: 'decodetree')

if 'CONFIG_TCG' in config_all
  subdir('fp')
endif

if not get_option('tcg').disabled()
  if 'CONFIG_PLUGIN' in config_host
    subdir('plugin')
  endif
endif

subdir('unit')
subdir('qapi-schema')
subdir('qtest')
subdir('migration')
