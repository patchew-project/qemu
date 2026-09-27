.. SPDX-License-Identifier: GPL-2.0-or-later

STM32L496G-DISCO Discovery Kit (``stm32l496g-disco``)
=====================================================

The STM32L496G-DISCO Discovery Kit uses an STM32L496AG microcontroller with
an Arm Cortex-M4F core, 1 MiB of internal flash, 256 KiB of SRAM1, and 64 KiB
of SRAM2.

Supported devices
-----------------

The machine initially supports the devices shared with the STM32L4x5 model:

- Cortex-M4F processor
- Internal flash and SRAM
- Reset and clock control
- EXTI and system configuration controllers
- GPIO ports A through H
- USART1, USART2, USART3, UART4, UART5, and LPUART1
- QuadSPI controller in indirect mode
- MX25R6435F QuadSPI flash

Missing devices
---------------

The following board features are not currently modeled:

- GPIO port I
- Timers, RTC, watchdogs, ADC, and DAC
- I2C, SPI, SDMMC, USB, and FMC controllers
- On-board display, joystick, and sensors

Boot options
------------

USART2 is the console used by the Zephyr board configuration. It is QEMU's
second serial backend, so start a Zephyr image with:

.. code-block:: bash

   qemu-system-arm -M stm32l496g-disco -kernel zephyr.elf \
     -nographic -serial null -serial stdio
