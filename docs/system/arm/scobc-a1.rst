.. SPDX-License-Identifier: GPL-2.0-or-later

Space Cubics SC-OBC Module A1 (``scobc-a1``)
============================================

The Space Cubics OBC Module A1 (SC-OBC Module A1) is an on-board computer
module for spacecraft, particularly 3U CubeSats. It is based on a Xilinx
Artix-7 FPGA, which implements an Arm Cortex-M3 as the main CPU.

The module must be mounted on a base I/O board. The ``scobc-a1`` machine
models only the module running the default FPGA image provided by Space
Cubics. The base board and its devices are not modelled.

The peripherals between ``0x5000_0000`` and ``0x6000_0000`` form the mission
bus, which users can customize by building their own FPGA image.

Supported devices
"""""""""""""""""

- Cortex-M3 CPU core
- 4 MiB HRMEM main memory
- HRMEM mirror (``0x6000_0000``)
- Xilinx UART Lite console UART (``0x4f01_0000``)
- 6 Xilinx UART Lite mission bus UARTs (``0x5000_0000`` - ``0x5005_0000``)

Missing devices
"""""""""""""""

The following devices are present in the memory map but not implemented:

- QSPI controllers for configuration flash, data flash and FRAM
- CAN controller
- HRMEM ECC and scrubbing registers
- System registers
- I2C controllers
- System monitor (FPGA watchdog, SEM controller, XADC and board health
  monitor)
- General purpose timer
- Mission bus I2C, SPI and GPIO controllers

Serial ports
""""""""""""

The UARTs are connected to the ``-serial`` options in the following order:

- ``serial0``: console UART (``0x4f01_0000``)
- ``serial1`` - ``serial6``: mission bus UART 1 - 6 (``0x5000_0000`` -
  ``0x5005_0000``)

Boot options
""""""""""""

The machine only supports direct boot into HRMEM. A firmware image
can be loaded with the ``-kernel`` option, for example a Zephyr application
built for the ``scobc_a1`` board:

.. code-block:: bash

  $ qemu-system-arm -M scobc-a1 -nographic -kernel zephyr.elf
