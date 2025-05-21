project('bitfield-struct-0.9-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '0.9.5',
  license: 'MIT',
  default_options: [])

subproject('quote-1-rs', required: true)
subproject('syn-2-rs', required: true)
subproject('proc-macro2-1-rs', required: true)

quote_dep = dependency('quote-1-rs', native: true)
syn_dep = dependency('syn-2-rs', native: true)
proc_macro2_dep = dependency('proc-macro2-1-rs', native: true)

rust = import('rust')

_bitfield_struct_rs = rust.proc_macro(
  'bitfield_struct',
  files('src/lib.rs'),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_args: [
    '--cap-lints', 'allow',
    '--cfg', 'use_fallback',
  ],
  dependencies: [
    quote_dep,
    syn_dep,
    proc_macro2_dep,
  ],
)

bitfield_struct_dep = declare_dependency(
  link_with: _bitfield_struct_rs,
)

meson.override_dependency('bitfield-struct-0.9-rs', bitfield_struct_dep)
