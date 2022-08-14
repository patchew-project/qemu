:orphan:

=================================================
LoongArch generic virtualized platform (``virt``)
=================================================

The ``virt`` machine has a GPEX host bridge, and some more emulated devices
such as the LS7A RTC, IOAPIC, ACPI device and so on.

Being a machine type designed for virtualized use cases, the machine resembles
a Loongson 3A5000 + LS7A1000 board, but is not an exact emulation.
For example, only cascading of the EXTIOI interrupt is implemented.
Also, only the RTC block of the LS7A1000 is emulated; for the other devices
the QEMU models are used.
Normally you do not need to care about any of these.

Supported devices
-----------------

The ``virt`` machine supports:

- GPEX host bridge
- LS7A RTC device
- LS7A IOAPIC device
- LS7A ACPI device
- fw_cfg device
- PCI/PCIe devices
- Memory device
- CPU device. Defaults to ``qemu64-v1.00``.

Boot options
------------

Some more information could be found in the QEMU sources at
``target/loongarch/README.md``. A simple example being:

.. code-block:: bash

  $ qemu-system-loongarch64 -machine virt -m 4G -smp 1 -kernel hello \
      -monitor none -display none \
      -chardev file,path=hello.out,id=output -serial chardev:output
