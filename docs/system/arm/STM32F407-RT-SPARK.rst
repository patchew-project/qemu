STM32F407-RT-SPARK (``stm32_f4spark``)
============================================

The STM32F407-RT-SPARK uses the STM32F407ZG SoC which is based on
ARM Cortex-M4 core. TThe STM32F407 series runs at up to 168 MHz,
integrating 196 KiB of SRAM (including 64 KiB CCM) and 1 MiB of
on-chip Flash. The STM32F407-RT-SPARK board further features
8 MiB NorFlash, an SD card holder, USB, RS-485, CAN bus.It also
integrates the RW007 SPI high-speed Wi-Fi module, providing
convenient network connectivity for IoT and RTOS development.

Supported devices
"""""""""""""""""

Currently STM32F407-RT-SPARK machines support the following devices:

- Cortex-M4 based STM32F407 SoC
- stm32f4xx EXTI (Extended interrupts and events controller)
- stm32f2xx SYSCFG (System configuration controller)
- stm32 RCC (Reset and clock control)
- stm32f2xx USARTs, UARTs and LPUART (Serial ports)

Missing devices
"""""""""""""""

The STM32F407-RT-SPARK does *not* support the following devices:

- Analog to Digital Converter (ADC)
- SPI controller
- Timer controller (TIMER)
- GPIOs (General-purpose I/Os)

Boot options
""""""""""""

The STM32F407-RT-SPARK machine can be started using the ``-kernel``
option to load a firmware. Example:

.. code-block:: bash

  $ qemu-system-arm -M rt-spark -kernel firmware.bin
