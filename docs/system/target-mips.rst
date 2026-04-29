.. _MIPS-System-emulator:

MIPS System emulator
--------------------

Four executables cover simulation of 32 and 64-bit MIPS systems in both
endian options, ``qemu-system-mips``, ``qemu-system-mipsel``
``qemu-system-mips64`` and ``qemu-system-mips64el``. Five different
machine types are emulated:

-  The MIPS Malta prototype board \"malta\"

-  An ACER Pica \"pica61\". This machine needs the 64-bit emulator.

-  A MIPS Magnum R4000 machine \"magnum\". This machine needs the
   64-bit emulator.

-  The Microchip PIC32MK GPK/MCM microcontroller family \"pic32mk\"

The Malta emulation supports the following devices:

-  Core board with MIPS 24Kf CPU and Galileo system controller

-  PIIX4 PCI/USB/SMbus controller

-  The Multi-I/O chip's serial device

-  PCI network cards (PCnet32 and others)

-  Malta FPGA serial device

-  Cirrus (default) or any other PCI VGA graphics card

The Boston board emulation supports the following devices:

-  Xilinx FPGA, which includes a PCIe root port and an UART

-  Intel EG20T PCH connects the I/O peripherals, but only the SATA bus
   is emulated

The ACER Pica emulation supports:

-  MIPS R4000 CPU

-  PC-style IRQ and DMA controllers

-  PC Keyboard

-  IDE controller

The MIPS Magnum R4000 emulation supports:

-  MIPS R4000 CPU

-  PC-style IRQ controller

-  PC Keyboard

-  SCSI controller

-  G364 framebuffer

The Fuloong 2E emulation supports:

-  Loongson 2E CPU

-  Bonito64 system controller as North Bridge

-  VT82C686 chipset as South Bridge

-  RTL8139D as a network card chipset

The Loongson-3 virtual platform emulation supports:

-  Loongson 3A CPU

-  LIOINTC as interrupt controller

-  GPEX and virtio as peripheral devices

-  Both KVM and TCG supported

The PIC32MK GPK/MCM emulation supports the following devices:

-  MIPS32 microAptiv MCU core (74Kf, little-endian, 120 MHz)

-  256 KB SRAM, 1 MB program flash, 256 KB boot flash

-  EVIC — 216-source vectored interrupt controller

-  UART × 6 (UART1 connected to the first serial port by default)

-  Timer × 9 with prescaler and period-match interrupts

-  GPIO ports A–G with TRIS/LAT/PORT/ANSEL/CNPU/CNPD registers

-  SPI × 6 (master and slave modes)

-  I2C × 4

-  DMA × 8 channels

-  CAN FD × 4 (via QEMU ``can-bus`` objects, SocketCAN-compatible)

-  USB Full-Speed OTG × 2 (chardev PTY)

-  ADCHS — 12-bit high-speed ADC with 7 cores

-  NVM flash controller with optional host-file backing

-  Data EEPROM emulation over program flash

-  Output Compare (OC) × 16 and Input Capture (IC) × 16

-  CRU (Clock and Reset Unit), WDT (Watchdog), CFG (configuration)

Running a firmware image::

   qemu-system-mipsel -M pic32mk -bios firmware.bin \
       -serial stdio -nographic -monitor none

Connecting to a SocketCAN interface (e.g. ``vcan0``)::

   qemu-system-mipsel -M pic32mk -bios firmware.bin \
       -object can-bus,id=canbus0 \
       -object can-host-socketcan,id=canhost0,if=vcan0,canbus=canbus0 \
       -serial stdio -nographic

Firmware must be a raw binary linked to start at 0xBFC40000
(Boot Flash 1). The reset vector at 0xBFC00000 contains a trampoline
that jumps to 0xBFC40000.

.. include:: cpu-models-mips.rst.inc

.. _nanoMIPS-System-emulator:

nanoMIPS System emulator
~~~~~~~~~~~~~~~~~~~~~~~~

Executable ``qemu-system-mipsel`` also covers simulation of 32-bit
nanoMIPS system in little endian mode:

-  nanoMIPS I7200 CPU

Example of ``qemu-system-mipsel`` usage for nanoMIPS is shown below:

Download ``<disk_image_file>`` from
https://mipsdistros.mips.com/LinuxDistro/nanomips/buildroot/index.html.

Download ``<kernel_image_file>`` from
https://mipsdistros.mips.com/LinuxDistro/nanomips/kernels/v4.15.18-432-gb2eb9a8b07a1-20180627102142/index.html.

Start system emulation of Malta board with nanoMIPS I7200 CPU::

   qemu-system-mipsel -cpu I7200 -kernel <kernel_image_file> \
       -M malta -serial stdio -m <memory_size> -drive file=<disk_image_file>,format=raw \
       -append "mem=256m@0x0 rw console=ttyS0 vga=cirrus vesa=0x111 root=/dev/sda"
