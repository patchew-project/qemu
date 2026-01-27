_lib_cmsdk_apb_watchdog = static_library(
  'cmsdk_apb_watchdog',
  files('src/device.rs'),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  link_with: [
    _util_rs,
    _bql_rs,
    _qom_rs,
    _system_rs,
    _hwcore_rs,
  ],
  dependencies: [common_rs],
)

rust_devices_ss.add(declare_dependency(
  link_whole: [_lib_cmsdk_apb_watchdog],
  variables: {'crate': 'cmsdk_apb_watchdog'},
))