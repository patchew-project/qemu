# TODO: Remove this comment when the clang/libclang mismatch issue is solved.
#
# Rust bindings generation with `bindgen` might fail in some cases where the
# detected `libclang` does not match the expected `clang` version/target. In
# this case you must pass the path to `clang` and `libclang` to your build
# command invocation using the environment variables CLANG_PATH and
# LIBCLANG_PATH
_libpcf8574_rs = static_library(
  'pcf8574',
  structured_sources(
    [
      'src/lib.rs',
    ],
  ),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  dependencies: [
    bilge_rs,
    bilge_impl_rs,
    bits_rs,
    common_rs,
    glib_sys_rs,
    util_rs,
    migration_rs,
    bql_rs,
    qom_rs,
    chardev_rs,
    system_rs,
    hwcore_rs,
    trace_rs
  ],
)

rust_devices_ss.add(when: 'CONFIG_X_PCF8574_RUST', if_true: [declare_dependency(
  link_whole: [_libpcf8574_rs],
  variables: {'crate': 'pcf8574'},
)])
