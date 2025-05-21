_bits_rs = static_library(
  'bits',
  'src/lib.rs',
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  dependencies: [qemu_api_macros],
)

bits_rs = declare_dependency(link_with: _bits_rs)

rust.test('rust-bits-tests', _bits_rs,
          suite: ['unit', 'rust'])
