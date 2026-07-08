project('vm-memory-0.18-rs', 'rust',
  meson_version: '>=1.5.0',
  version : '0.18.0',
  license : 'Apache-2.0 or BSD-3-Clause',
  default_options: [])

all_deps = []

subproject('thiserror-1-rs', required: true)
all_deps += dependency('thiserror-1-rs')

_vm_memory_rs = static_library(
  'vm_memory',
  files('src/lib.rs'),
  gnu_symbol_visibility: 'hidden',
  override_options : ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi : 'rust',
  rust_args: ['--cap-lints', 'allow'],
  dependencies: all_deps,
  native: true,
)

dep = declare_dependency(
  link_with : _vm_memory_rs,
)

meson.override_dependency('vm-memory-0.18-rs', dep, native: true)
