_chardev_cfg = run_command(rustc_args,
  '--config-headers', config_host_h, '--features', files('Cargo.toml'),
  capture: true, check: true).stdout().strip().splitlines()

c_enums = [
  'QEMUChrEvent',
]
_chardev_bindgen_args = []
foreach enum : c_enums
  _chardev_bindgen_args += ['--rustified-enum', enum]
endforeach

# TODO: Remove this comment when the clang/libclang mismatch issue is solved.
#
# Rust bindings generation with `bindgen` might fail in some cases where the
# detected `libclang` does not match the expected `clang` version/target. In
# this case you must pass the path to `clang` and `libclang` to your build
# command invocation using the environment variables CLANG_PATH and
# LIBCLANG_PATH
_chardev_bindings_inc_rs = rust.bindgen(
  input: 'wrapper.h',
  dependencies: common_ss.all_dependencies(),
  output: 'bindings.inc.rs',
  include_directories: bindings_incdir,
  bindgen_version: ['>=0.60.0'],
  args: bindgen_args_common + _chardev_bindgen_args,
)

_chardev_rs = static_library(
  'chardev',
  structured_sources(
    [
      'src/lib.rs',
      'src/bindings.rs',
      'src/chardev.rs',
    ],
    {'.': _chardev_bindings_inc_rs}
  ),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  rust_args: _chardev_cfg,
  dependencies: [qemuutil_rs, common_rs, bql_rs, migration_rs, qemu_macros, qom_rs, util_rs, chardev],
)

chardev_rs = declare_dependency(link_with: [_chardev_rs], dependencies: [qemu_macros, chardev])

# Doctests are essentially integration tests, so they need the same dependencies.
# Note that running them requires the object files for C code, so place them
# in a separate suite that is run by the "build" CI jobs rather than "check".
rust.doctest('rust-chardev-rs-doctests',
     _chardev_rs,
     protocol: 'rust',
     dependencies: chardev_rs,
     suite: ['doc', 'rust'])
