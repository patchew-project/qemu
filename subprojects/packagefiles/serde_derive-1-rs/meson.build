project('serde_derive-1-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '1.0.226',
  license: 'MIT OR Apache-2.0',
  default_options: [])

subproject('quote-1-rs', required: true)
subproject('syn-2-rs', required: true)
subproject('proc-macro2-1-rs', required: true)

quote_dep = dependency('quote-1-rs', native: true)
syn_dep = dependency('syn-2-rs', native: true)
proc_macro2_dep = dependency('proc-macro2-1-rs', native: true)

rust = import('rust')

_serde_derive_rs = rust.proc_macro(
  'serde_derive',
  files('src/lib.rs'),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_args: [
    '--cap-lints', 'allow',
  ],
  dependencies: [
    quote_dep,
    syn_dep,
    proc_macro2_dep,
  ],
)

serde_derive_dep = declare_dependency(
  link_with: _serde_derive_rs,
)

meson.override_dependency('serde_derive-1-rs', serde_derive_dep)
