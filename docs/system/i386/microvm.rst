'microvm' virtual platform (``microvm``)
========================================

``microvm`` is a machine type inspired by ``Firecracker`` and
constructed after its machine model.

It's a minimalist machine type designed for short-lived
guests. microvm also establishes a baseline for benchmarking and
optimizing both QEMU and guest operating systems, since it is
optimized for both boot time and footprint.


Supported devices
-----------------

The microvm machine type supports the following devices:

- ISA bus
- i8259 PIC (optional)
- i8254 PIT (optional)
- MC146818 RTC (optional)
- One ISA serial port (optional)
- LAPIC
- IOAPIC (with kernel-irqchip=split by default)
- kvmclock (if using KVM)
- fw_cfg
- Up to 24 virtio-mmio devices (configured by the user).
- PCIe devices (optional).
- USB devices (optional).


Limitations
-----------

Currently, microvm does *not* support the following features:

- Hotplug of any kind.
- Live migration across QEMU versions.


Using the microvm machine type
------------------------------

Machine-specific options
~~~~~~~~~~~~~~~~~~~~~~~~

It supports the following machine-specific options:

- microvm.x-option-roms=bool (Set off to disable loading option ROMs)
- microvm.pit=OnOffAuto (Enable i8254 PIT)
- microvm.isa-serial=bool (Set off to disable the instantiation an ISA serial port)
- microvm.pic=OnOffAuto (Enable i8259 PIC)
- microvm.rtc=OnOffAuto (Enable MC146818 RTC)
- microvm.acpi=OnOff (Enable ACPI support, default On)
- microvm.auto-kernel-cmdline=bool (Set off to disable adding
  virtio-mmio devices to the kernel cmdline, enabling ACPI support
  disables this too).
- microvm.pcie=OnOff (Enable PCIe host adapter, default Off)
- microvm.usb=OnOff (Enable USB host adapter, default Off)
- microvm.ioapic2=OnOff (Enable second IOAPIC for virtio-mmio devices,
  required for more than eight virtio-mmio devices, default On)


Boot options
~~~~~~~~~~~~

By default, microvm uses ``SeaBIOS`` as its firmware. ``SeaBIOS``
supports booting from virtio-mmio devices.

edk2 has full support for microvm, including support for the PCIe host
adapter. so you can boot from both virtio-mmio and PCIe devices.


Running a microvm-based VM
~~~~~~~~~~~~~~~~~~~~~~~~~~

By default, microvm aims for maximum compatibility, enabling both
legacy and non-legacy devices. In this example, a VM is created
without passing any additional machine-specific option, using the
legacy ``ISA serial`` device as console::

  $ qemu-system-x86_64 -M microvm \
     -enable-kvm -cpu host -m 512m -smp 2 \
     -kernel vmlinux -append "earlyprintk=ttyS0 console=ttyS0 root=/dev/vda" \
     -nodefaults -no-user-config -nographic \
     -serial stdio \
     -drive id=test,file=test.img,format=raw,if=none \
     -device virtio-blk-device,drive=test \
     -netdev tap,id=tap0,script=no,downscript=no \
     -device virtio-net-device,netdev=tap0

While the example above works, you might be interested in reducing the
footprint further by disabling some legacy devices. If you're using
``KVM``, you can disable the ``RTC``, making the Guest rely on
``kvmclock`` exclusively. Additionally, if your host's CPUs have the
``TSC_DEADLINE`` feature, you can also disable both the i8259 PIC and
the i8254 PIT (make sure you're also emulating a CPU with such feature
in the guest).

This is an example of a VM with all optional legacy features
disabled::

  $ qemu-system-x86_64 \
     -M microvm,x-option-roms=off,pit=off,pic=off,isa-serial=off,rtc=off \
     -enable-kvm -cpu host -m 512m -smp 2 \
     -kernel vmlinux -append "console=hvc0 root=/dev/vda" \
     -nodefaults -no-user-config -nographic \
     -chardev stdio,id=virtiocon0 \
     -device virtio-serial-device \
     -device virtconsole,chardev=virtiocon0 \
     -drive id=test,file=test.img,format=raw,if=none \
     -device virtio-blk-device,drive=test \
     -netdev tap,id=tap0,script=no,downscript=no \
     -device virtio-net-device,netdev=tap0
