project('serde-1-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '1.0.226',
  license: 'MIT OR Apache-2.0',
  default_options: [])

subproject('serde_core-1-rs', required: true)
subproject('serde_derive-1-rs', required: true)

serde_core_dep = dependency('serde_core-1-rs')
serde_derive_dep = dependency('serde_derive-1-rs')

_serde_rs = static_library(
  'serde',
  files('src/lib.rs'),
  gnu_symbol_visibility: 'hidden',
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  rust_args: [
    '--cap-lints', 'allow',
    '--cfg', 'feature="alloc"',
    '--cfg', 'feature="std"',
    '--cfg', 'feature="derive"',
  ],
  dependencies: [
    serde_core_dep,
    serde_derive_dep,
  ]
)

serde_dep = declare_dependency(
  link_with: _serde_rs,
  dependencies: serde_derive_dep,
)

meson.override_dependency('serde-1-rs', serde_dep, native: true)
