_libpl011_rs = static_library(
  'pl011',
  files('src/lib.rs'),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  dependencies: [
    bitfield_struct_dep,
    bits_rs,
    qemu_api,
    qemu_api_macros,
  ],
)

rust_devices_ss.add(when: 'CONFIG_X_PL011_RUST', if_true: [declare_dependency(
  link_whole: [_libpl011_rs],
  # Putting proc macro crates in `dependencies` is necessary for Meson to find
  # them when compiling the root per-target static rust lib.
  dependencies: [bitfield_struct_dep, qemu_api_macros],
  variables: {'crate': 'pl011'},
)])
