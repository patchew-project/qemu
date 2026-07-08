project('thiserror-1-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '1.0.65',
  license: 'MIT OR Apache-2.0',
  default_options: [])

subproject('thiserror-impl-1-rs', required: true)
thiserror_impl_rs = dependency('thiserror-impl-1-rs')

_thiserror_rs = static_library(
  'thiserror',
  files('src/lib.rs'),
  gnu_symbol_visibility: 'hidden',
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  dependencies: [thiserror_impl_rs],
)

thiserror_dep = declare_dependency(
  link_with: _thiserror_rs,
)

meson.override_dependency('thiserror-1-rs', thiserror_dep)
