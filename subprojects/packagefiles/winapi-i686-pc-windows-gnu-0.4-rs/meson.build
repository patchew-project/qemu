project('winapi-i686-pc-windows-gnu-0.4-rs', 'rust',
 meson_version: '>=1.5.0',
 version: '0.4.0',
 license: 'MIT OR Apache-2.0',
 default_options: [])

lib = static_library(
  'winapi-i686-pc-windows-gnu',
  'src/lib.rs',
  override_options : ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi : 'rust',
  rust_args: ['--cap-lints', 'allow'],
  dependencies: [thiserror_rs],
)

dep = declare_dependency(
  link_with : [lib],
)

meson.override_dependency('winapi-i686-pc-windows-gnu-0.4-rs', dep)
