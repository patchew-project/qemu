# SPDX-License-Identifier: GPL-2.0-or-later

_libstm32f1xx_gpio_rs = cargo_ws.package('stm32f1xx_gpio').library()

rust_devices_ss.add(when: 'CONFIG_X_STM32F1XX_GPIO_RUST', if_true: [declare_dependency(
  link_whole: [_libstm32f1xx_gpio_rs],
  variables: {'crate': 'stm32f1xx_gpio'},
)])

stm32f1xx_gpio_rs = declare_dependency(
  link_with: [_libstm32f1xx_gpio_rs],
  dependencies: [
    common_rs,
    qom_rs,
    hwcore_rs,
    bql_rs,
    migration_rs,
    system_rs,
    util_rs,
  ],
)

test(
  'rust-stm32f1xx-gpio-integration',
  executable(
    'rust-stm32f1xx-gpio-integration',
    files('tests/tests.rs'),
    override_options: ['rust_std=2021', 'build.rust_std=2021'],
    rust_args: ['--test'],
    install: false,
    dependencies: [
      stm32f1xx_gpio_rs,
      common_rs,
      qom_rs,
      hwcore_rs,
      bql_rs,
      migration_rs,
      system_rs,
      util_rs,
    ],
  ),
  args: [
    '--test',
    '--test-threads', '1',
    '--format', 'pretty',
  ],
  protocol: 'rust',
  suite: ['unit', 'rust'],
)
