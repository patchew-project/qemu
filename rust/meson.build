# Supported hosts
rust_supported_oses = {
  'linux': '-unknown-linux-gnu',
  'darwin': '-apple-darwin',
  'windows': '-pc-windows-gnu'
}
rust_supported_cpus = ['x86_64', 'aarch64']

# Future-proof the above definitions against any change in the root meson.build file:
foreach rust_os: rust_supported_oses.keys()
  if not supported_oses.contains(rust_os)
    message()
    warning('UNSUPPORTED OS VALUES IN ' + meson.current_source_dir() + '/meson.build')
    message()
    message('This meson.build file claims OS `+' + rust_os + '` is supported but')
    message('it is not included in the global supported OSes list in')
    message(meson.global_source_root() + '/meson.build.')
  endif
endforeach
foreach rust_cpu: rust_supported_cpus
  if not supported_cpus.contains(rust_cpu)
    message()
    warning('UNSUPPORTED CPU VALUES IN ' + meson.current_source_dir() + '/meson.build')
    message()
    message('This meson.build file claims CPU `+' + rust_cpu + '` is supported but')
    message('it is not included in the global supported CPUs list in')
    message(meson.global_source_root() + '/meson.build.')
  endif
endforeach

# FIXME: retrieve target triple from meson and construct rust_target_triple
if meson.is_cross_build()
  message()
  error('QEMU does not support cross compiling with Rust enabled.')
endif

# FIXME: These are the latest stable versions, refine to actual minimum ones.
msrv = {
  'rustc': '1.79.0',
  'cargo': '1.79.0',
  'bindgen': '0.69.4',
}

# rustup = find_program('rustup', required: false)
foreach bin_dep: msrv.keys()
  bin = find_program(bin_dep, required: true)
  if bin.version() < msrv[bin_dep]
    if bin_dep == 'bindgen' and get_option('wrap_mode') != 'nodownload'
      run_command(cargo, 'install', 'bindgen-cli', capture: true, check: true)
      bin = find_program(bin_dep, required: true)
      if bin.version() >= msrv[bin_dep]
        continue
      endif
    endif
    message()
    error(bin_dep + ' version ' + bin.version() + ' is unsupported: Please upgrade to at least ' + msrv[bin_dep])
  endif
endforeach

rust_target_triple = get_option('with_rust_target_triple')

if rust_target_triple == ''
  if not supported_oses.contains(host_os)
    message()
    error('QEMU does not support `' + host_os +'` as a Rust platform.')
  elif not supported_cpus.contains(host_arch)
    message()
    error('QEMU does not support `' + host_arch +'` as a Rust architecture.')
  endif
  rust_target_triple = host_arch + rust_supported_oses[host_os]
  if host_os == 'windows' and host_arch == 'aarch64'
    rust_target_triple += 'llvm'
  endif
else
  # verify rust_target_triple if given as an option
  rustc = find_program('rustc', required: true)
  rustc_targets = run_command(rustc, '--print', 'target-list', capture: true, check: true).stdout().strip().split()
  if not rustc_targets.contains(rust_target_triple)
    message()
    error('Given rust_target_triple ' + rust_target_triple + ' is not listed in rustc --print target-list output')
  endif
endif


rust_targets = {}

cargo_wrapper = [
  find_program(meson.global_source_root() / 'scripts/cargo_wrapper.py'),
  '--config-headers', meson.project_build_root() / 'config-host.h',
  '--meson-build-root', meson.project_build_root(),
  '--meson-build-dir', meson.current_build_dir(),
  '--meson-source-dir', meson.current_source_dir(),
]

if get_option('b_colorout') != 'never'
  cargo_wrapper += ['--color', 'always']
endif

if get_option('optimization') in ['0', '1', 'g']
  rs_build_type = 'debug'
else
  rs_build_type = 'release'
endif

# Collect metadata for each (crate,qemu-target,compiler-target) combination.
# Rust meson targets cannot be defined a priori because they depend on bindgen
# generation that is created for each emulation target separately. Thus Rust
# meson targets will be defined for each target after the target-specific
# bindgen dependency is declared.
rust_hw_target_list = {}

subdir('pl011')

foreach rust_hw_target, rust_hws: rust_hw_target_list
  foreach rust_hw_dev: rust_hws
    output = meson.current_build_dir() / rust_target_triple / rs_build_type / rust_hw_dev['output']
    crate_metadata = {
      'name': rust_hw_dev['name'],
      'output': [rust_hw_dev['output']],
      'output-path': output,
      'command': [cargo_wrapper,
        '--crate-dir', meson.current_source_dir() / rust_hw_dev['dirname'],
        '--profile', rs_build_type,
        '--target-triple', rust_target_triple,
        '--outdir', '@OUTDIR@',
        'build-lib'
        ]
      }
    rust_targets += { rust_hw_target: [crate_metadata] }
  endforeach
endforeach
