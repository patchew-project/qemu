Raspberry Pi boards (``raspi0``, ``raspi1ap``, ``raspi2b``, ``raspi3ap``, ``raspi3b``, ``raspi4b``)
===================================================================================================


QEMU provides models of the following Raspberry Pi boards:

``raspi0`` and ``raspi1ap``
  ARM1176JZF-S core, 512 MiB of RAM
``raspi2b``
  Cortex-A7 (4 cores), 1 GiB of RAM
``raspi3ap``
  Cortex-A53 (4 cores), 512 MiB of RAM
``raspi3b``
  Cortex-A53 (4 cores), 1 GiB of RAM
``raspi4b``
  Cortex-A72 (4 cores), 2 GiB of RAM

Implemented devices
-------------------

 * ARM1176JZF-S, Cortex-A7, Cortex-A53 or Cortex-A72 CPU
 * Interrupt controller
 * DMA controller
 * Clock and reset controller (CPRMAN)
 * System Timer
 * GPIO controller
 * Serial ports (BCM2835 AUX - 16550 based - and PL011)
 * Random Number Generator (RNG)
 * Frame Buffer
 * USB host (USBH)
 * GPIO controller
 * SD/MMC host controller
 * SoC thermal sensor
 * USB2 host controller (DWC2 and MPHI)
 * MailBox controller (MBOX)
 * VideoCore firmware (property)
 * Peripheral SPI controller (SPI)
 * Broadcom Serial Controller (I2C)

Missing devices
---------------

 * Pulse Width Modulation (PWM)
 * PCIE Root Port (raspi4b)
 * GENET Ethernet Controller (raspi4b)

Boot options
------------

The Raspberry Pi models support booting via either a Linux kernel (``-kernel``)
or UEFI/EDK2 firmware images (``-bios rpi_efi.fd``).

When booting Raspberry Pi 3 (``raspi3b``, ``raspi3ap``) and Raspberry Pi 4 (``raspi4b``)
boards with EDK2 firmware via ``-bios``, the firmware image is loaded at physical
memory address ``0x0``, matching the platform expectations. Older 32-bit models
(``raspi0``, ``raspi1ap``, ``raspi2b``) load firmware at their respective legacy addresses
(``0x8000``).
