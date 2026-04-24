.. SPDX-License-Identifier: GPL-2.0-or-later

RISC-V Server Platform Reference board (``riscv-server-ref``)
=============================================================

The RISC-V Server Platform specification `spec`_ defines a standardized
set of hardware and software capabilities that portable system software,
such as OS and hypervisors, can rely on being present in a RISC-V server
platform.  This machine aims to emulate this specification, providing
an environment for firmware/OS development and testing.

The main features included in the riscv-server-ref board are:

* IOMMU platform device (riscv-iommu-sys)
* AIA
* PCIe AHCI
* PCIe NIC
* No virtio mmio bus
* No fw_cfg device
* No ACPI table
* Minimal device tree nodes

There are multiple ways of using this reference board, some of them being:

* BIOS: u-boot-spl.bin; SD-CARD: <disk_containing_FIT>; NVME: <distro_disk>
* BIOS: fw_dynamic.bin; KERNEL: u-boot.bin; NVME: <distro_disk>
* BIOS: fw_dynamic.bin; KERNEL: EDK2.fd; NVME: <distro_disk>
* BIOS: fw_dynamic.bin; KERNEL: <linux_image>; INITRD: <busybox_initrd>


.. _spec: https://github.com/riscv-non-isa/riscv-server-platform
