test_qapi_rs_files = custom_target('QAPI Rust',
  output: 'test-qapi-types.rs',
  input: [ files(meson.project_source_root() + '/tests/qapi-schema/qapi-schema-test.json') ],
  command: [ qapi_gen, '-o', meson.current_build_dir(), '-b', '@INPUT0@', '-B', 'qapi.backend.QAPIRsBackend', '-p', 'test-' ],
  depend_files: [ qapi_inputs, qapi_gen_depends ])

_test_qapi_rs = static_library(
    'test_qapi',
    test_qapi_rs_files,
    override_options: ['rust_std=2021', 'build.rust_std=2021'],
    rust_abi: 'rust',
    dependencies: [common_rs, util_rs, serde_rs, serde_derive_rs])

test_qapi_rs = declare_dependency(link_with: [_test_qapi_rs])

test('rust-integration',
    executable(
        'rust-integration',
        files(
            'tests/integration.rs',
            'tests/vmstate_tests.rs',
            'tests/qapi.rs',
        ),
        override_options: ['rust_std=2021', 'build.rust_std=2021'],
        rust_args: ['--test'] + _qapi_cfg,
        install: false,
        dependencies: [bql_rs, common_rs, util_rs, migration_rs, qom_rs, qapi_rs, test_qapi_rs]),
    args: [
        '--test', '--test-threads', '1',
        '--format', 'pretty',
    ],
    protocol: 'rust',
    suite: ['unit', 'rust'])
