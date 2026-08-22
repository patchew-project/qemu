# SPDX-License-Identifier: GPL-2.0-or-later

_libstm32f1xx_gpio_rs = cargo_ws.package('stm32f1xx_gpio').library()

rust_devices_ss.add(when: 'CONFIG_X_STM32F1XX_GPIO_RUST', if_true: [declare_dependency(
  link_whole: [_libstm32f1xx_gpio_rs],
  variables: {'crate': 'stm32f1xx_gpio'},
)])
