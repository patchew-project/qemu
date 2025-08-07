project('thiserror-impl-1-rs', 'rust',
 meson_version: '>=1.5.0',
 version: '1.0.65',
 license: 'MIT OR Apache-2.0',
 default_options: [])

subproject('quote-1-rs', required: true)
subproject('syn-2-rs', required: true)
subproject('proc-macro2-1-rs', required: true)

quote_dep = dependency('quote-1-rs', native: true)
syn_dep = dependency('syn-2-rs', native: true)
proc_macro2_dep = dependency('proc-macro2-1-rs', native: true)

rust = import('rust')

_thiserror_impl_rs = rust.proc_macro(
  'thiserror_impl',
  files('src/lib.rs'),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_args: [
    '--cfg', 'feature="proc-macro"',
    '--cfg', 'feature="clone-impls"',
    '--cfg', 'feature="derive"',
    '--cfg', 'feature="extra-traits"',
    '--cfg', 'feature="full"',
    '--cfg', 'feature="parsing"',
    '--cfg', 'feature="printing"',
  ],
  dependencies: [
    quote_dep,
    syn_dep,
    proc_macro2_dep
  ],
)

thiserror_impl_dep = declare_dependency(
  link_with: _thiserror_impl_rs,
)

meson.override_dependency('thiserror-impl-1-rs', thiserror_impl_dep)
