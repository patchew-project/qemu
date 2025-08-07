project('winapi-0.3-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '0.3.9',
  license: 'MIT OR Apache-2.0'
)

if host_machine.cpu_family() == 'x86_64'
  winapi_arch = 'winapi-x86_64-pc-windows-gnu-0.4-rs'
elif host_machine.cpu_family() == 'x86'
  winapi_arch = 'winapi-i686-pc-windows-gnu-0.4-rs'
else
  error('Unsupported CPU family for winapi: ' + host_machine.cpu_family())
endif

subproject(winapi_arch, required: true)
winapi_arch_dep = dependency(winapi_arch)

winapi_features = [
  '--cfg', 'feature="errhandlingapi"',
  '--cfg', 'feature="sysinfoapi"',
  '--cfg', 'feature="excpt"',
  '--cfg', 'feature="minwinbase"',
  '--cfg', 'feature="ntstatus"',
  '--cfg', 'feature="winnt"',
  '--cfg', 'feature="basetsd"',
  '--cfg', 'feature="ktmtypes"',
  '--cfg', 'feature="minwindef"',
  '--cfg', 'feature="ntdef"',
  '--cfg', 'feature="guiddef"',
  '--cfg', 'feature="vcruntime"'
]

lib = static_library(
  'winapi',
  'src/lib.rs',
  override_options : ['rust_std=2021'],
  rust_abi : 'rust',
  rust_args: ['--cap-lints', 'allow'] + winapi_features,
  dependencies: [winapi_arch_dep]
)

dep = declare_dependency(
  link_with: lib,
)

meson.override_dependency('winapi-0.3-rs', dep)
