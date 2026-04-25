Tenstorrent Atlantis (``tt-atlantis``)
======================================

The Tenstorrent Atlantis platform is a collaboration between Tenstorrent
and CoreLab Technology. It is based on the Atlantis SoC, which includes
the Ascalon-X CPU and other IP from Tenstorrent and CoreLab Technology.

The Tenstorrent Ascalon-X is a high performance 64-bit RVA23 compliant
RISC-V CPU.

Features
--------

* 8-core Ascalon-X CPU Cluster
* Dual x32 LPDDR5 @ 6400 MT/s
* RISC-V compliant Advanced Interrupt Architecture
* PCIe Gen4
* RISC-V compliant IOMMU
* GPU and Video subsystem
* 2x USB3.1 & 2x USB2.0
* 2x 1GbE Ethernet
* 2x eMMC5.1/SDIO3.0 storage
* Extensive connectivity (SPI, I2C, UART, GPIO, CANFD)

Note: the QEMU tt-atlantis machine does not model the platform
exactly or all devices, but it is undergoing improvement.

Supported software
------------------

The Tenstorrent Ascalon CPUs avoid proprietary or non-standard
extensions, so compatibility with existing software is generally
good. The QEMU tt-atlantis machine works with upstream OpenSBI
and Linux with default configurations.

The development board hardware will require some implementation
specific setup in firmware which is being developed and may
become a requirement or option for the tt-atlantis machine.
