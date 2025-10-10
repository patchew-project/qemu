project('serde_core-1-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '1.0.226',
  license: 'MIT OR Apache-2.0',
  default_options: [])

_serde_core_rs = static_library(
  'serde_core',
  files('src/lib.rs'),
  gnu_symbol_visibility: 'hidden',
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  rust_args: [
    '--cap-lints', 'allow',
    '--cfg', 'feature="alloc"',
    '--cfg', 'feature="result"',
    '--cfg', 'feature="std"',
  ],
)

serde_core_dep = declare_dependency(
  link_with: _serde_core_rs,
)

meson.override_dependency('serde_core-1-rs', serde_core_dep, native: true)
