_util_rs = static_library(
  'util',
  'src/lib.rs',
  dependencies: [anyhow_rs, libc_rs, foreign_rs, glib_sys_rs, common_rs, serde_rs, util_sys_rs],
)

util_rs = declare_dependency(link_with: [_util_rs], dependencies: [qemuutil, qom])

rust.test('rust-util-tests', _util_rs,
          dependencies: [qemuutil, qom],
          suite: ['unit', 'rust'])

# Doctests are essentially integration tests, so they need the same dependencies.
# Note that running them requires the object files for C code, so place them
# in a separate suite that is run by the "build" CI jobs rather than "check".
rust.doctest('rust-util-rs-doctests',
     _util_rs,
     dependencies: util_rs,
     suite: ['doc', 'rust']
)

_qapi_cfg = run_command(rustc_args,
  '--no-strict-cfg',
  '--config-headers', config_host_h,
  capture: true, check: true).stdout().strip().splitlines()

_qapi_rs = static_library(
  'qapi',
  qapi_rs_files,
  rust_args: _qapi_cfg,
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  link_with: [_util_rs],
  dependencies: [common_rs, serde_rs],
)

qapi_rs = declare_dependency(link_with: [_qapi_rs])
