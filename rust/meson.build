rust_targets = {}

cargo_wrapper = [
  find_program(meson.source_root() / 'scripts/cargo_wrapper.py'),
  '--config-headers', meson.project_build_root() / 'config-host.h',
  '--meson-build-dir', meson.current_build_dir(),
  '--meson-source-dir', meson.current_source_dir(),
]

if get_option('b_colorout') != 'never'
  cargo_wrapper += ['--color', 'always']
endif

if with_rust
  rust_target_triple = get_option('with_rust_target_triple')
  if meson.is_cross_build()
    # more default target mappings may be added over time
    if rust_target_triple == '' and targetos == 'windows'
      rust_target_triple = host_machine.cpu() + '-pc-windows-gnu'
    endif
    if rust_target_triple == ''
      error('cross-compiling, but no Rust target-triple defined.')
    endif
  endif
endif

if get_option('optimization') in ['0', '1', 'g']
  rs_build_type = 'debug'
else
  rs_build_type = 'release'
endif

_host_target_list = [['x86_64-unknown-linux-gnu', 'aarch64-softmmu']]

foreach targets: _host_target_list
  host_target = targets[0]
  target = targets[1]
  output = meson.current_build_dir() / host_target / rs_build_type / 'libpl011.a'
  if rust_target_triple == ''
    rust_target_triple = host_target
  endif
  rust_pl011_cargo = custom_target('rust_pl011_cargo',
                               output: ['libpl011.a'],
                               build_always_stale: true,
                               depends: [],
                               command: [cargo_wrapper,
                                         '--crate-dir',
                                         meson.current_source_dir() / 'pl011',
                                         '--profile',
                                         rs_build_type,
                                         '--target-triple',
                                         rust_target_triple,
                                         '--outdir',
                                         '@OUTDIR@',
                                         'build-lib'
                                         ]
                               )
  rust_pl011 = declare_dependency(link_args : [
                                  '-Wl,--whole-archive',
                                  output,
                                  '-Wl,--no-whole-archive'
                                  ],
                                  sources: [rust_pl011_cargo])
  rust_targets += { target: [rust_pl011] }
endforeach
