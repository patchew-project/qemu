.. _riscv-iommu:

RISC-V IOMMU support for RISC-V machines
========================================

QEMU implements a RISC-V IOMMU emulation based on the RISC-V IOMMU spec
version 1.0 [1].

The emulation includes a PCI reference device, riscv-iommu-pci, that QEMU
RISC-V boards can use.  The 'virt' RISC-V machine is compatible with this
device.

A platform device that implements the RISC-V IOMMU will be added in the
future.


riscv-iommu-pci reference device
--------------------------------

This device implements the RISC-V IOMMU emulation as recommended by the section
"Integrating an IOMMU as a PCIe device" of [1]: a PCI device with base class 08h,
sub-class 06h and programming interface 00h.

As a reference device it doesn't implement anything outside of the specification,
so it uses a generic default PCI ID given by QEMU: 1b36:0014.

To include the device in the 'virt' machine:

.. code-block:: bash

  $ qemu-system-riscv64 -M virt -device riscv-iommu-pci (...)

As of this writing the existing Linux kernel support [2], not yet merged, is being
created as a Rivos device, i.e. it uses Rivos vendor ID.  To use the riscv-iommu-pci
device with the existing kernel support we need to emulate a Rivos PCI IOMMU by
setting 'vendor-id' and 'device-id':

.. code-block:: bash

  $ qemu-system-riscv64 -M virt	\
     -device riscv-iommu-pci,vendor-id=0x1efd,device-id=0xedf1 (...)

Several options are available to control the capabilities of the device, namely:

- "bus"
- "ioatc-limit"
- "intremap"
- "ats"
- "off" (Out-of-reset translation mode: 'on' for DMA disabled, 'off' for 'BARE' (passthrough))
- "s-stage"
- "g-stage"


[1] https://github.com/riscv-non-isa/riscv-iommu/releases/download/v1.0/riscv-iommu.pdf
[2] https://lore.kernel.org/linux-riscv/cover.1718388908.git.tjeznach@rivosinc.com/
