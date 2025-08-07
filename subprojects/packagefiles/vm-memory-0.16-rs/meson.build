project(
  'vm-memory-0.16-rs',
  'rust',
  meson_version: '>=1.5.0',
  #version : '0.2.0',
  license : 'Apache-2.0 or BSD-3-Clause',
)

all_deps = []

subproject('thiserror-1-rs', required: true)
all_deps += dependency('thiserror-1-rs')

if host_machine.system() == 'windows'
  subproject('winapi-0.3-rs', required: true)
  all_deps += dependency('winapi-0.3-rs')
endif

# Note "rawfd" (as the only default feature) is disabled by default in
# meson. It cause compilation failure on Windows and fortunately, we
# don't need it either.
lib = static_library(
  'vm_memory',
  'src/lib.rs',
  override_options : ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi : 'rust',
  rust_args: ['--cap-lints', 'allow'],
  dependencies: all_deps,
)

dep = declare_dependency(
  link_with : [lib],
)

meson.override_dependency('vm-memory-0.16-rs', dep)
