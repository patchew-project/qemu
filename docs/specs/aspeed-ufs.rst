ASPEED AST2700 UFS Host Controller
===================================

The AST2700 SoC includes a UFS host controller identified in the device tree
as ``aspeed,ufshc-m31-16nm``, mapped at ``0x12c08200`` (IRQ SPI 118). QEMU
models it as a sysbus frontend on top of the shared UFS core that also backs
the PCI UFS device (``hw/ufs/ufs.c`` and ``hw/ufs/lu.c``). The frontend only
provides the sysbus MMIO region, the interrupt line and the DMA address
space; all UFSHCI register behaviour, UTP transfer/task list processing,
UPIU and query handling and the SCSI logical-unit logic are implemented by
the core.

The clock/reset wrapper at ``0x12c08000`` (``aspeed,ast2700-ufscnr``) is left
as an ``UnimplementedDevice``.

Logical units
-------------

Storage is attached through ``ufs-lu`` devices on the controller's UFS bus
(``ufs-bus.0``), exactly as for the PCI UFS device. The Huygens OpenBMC image
is laid out for 512-byte sectors, so its logical unit is created with
``logical-block-size=512``.

Usage
-----

Attach a UFS image as logical unit 0 of the controller's UFS bus:

.. code-block:: console

  qemu-system-aarch64 -M huygens-bmc \
    -nodefaults \
    -blockdev node-name=fmc0,driver=file,filename=image-bmc \
    -device w25q01jvq,bus=ssi.0,cs=0,drive=fmc0 \
    -blockdev node-name=ufs0,driver=file,filename=ufs.img \
    -device ufs-lu,bus=ufs-bus.0,drive=ufs0,lun=0,logical-block-size=512 \
    -display none -serial mon:stdio

Please check :doc:`../../system/arm/aspeed` for more details on the
``huygens-bmc`` machine.
