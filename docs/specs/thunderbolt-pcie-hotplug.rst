.. SPDX-License-Identifier: GPL-2.0-or-later

Thunderbolt PCIe hotplug layer
==============================

This series adds a Thunderbolt-flavoured PCIe layer for experiments with
guest-visible PCIe tunnelling.  Devices behind the layer remain normal QEMU
PCI devices; the Thunderbolt root port provides the guest-visible connection
state, ACPI properties, and PCIe hotplug notifications.

The main use case is hotplug and hot-unplug of real and virtual devices in
guests that cannot use direct hotplug for those devices.  For example, a macOS
guest can observe the endpoint through a guest-visible Thunderbolt layer when
direct PCIe hotplug for the same endpoint is not supported or not usable.

The implementation adds three user-visible device types:

``thunderbolt-root-port``
  PCIe root port that creates a ``thunderbolt-bus`` secondary bus.  The
  ``connected`` QOM property controls whether devices behind the port are
  reported as present to the guest.

``thunderbolt-pcie-pci-bridge``
  PCIe-to-PCI bridge for tunnelled PCI endpoints.  It adds Thunderbolt-related
  ACPI properties while leaving downstream PCI devices otherwise normal.

``thunderbolt-vga``
  Hotpluggable stdvga-compatible device for Thunderbolt experiments.  The
  regular ``VGA`` device is not made hotpluggable and its defaults are not
  changed.

Basic launch example
--------------------

Start the guest with its normal boot display and an empty Thunderbolt root
port:

::

  qemu-system-x86_64 \
      -machine q35 \
      -device VGA,id=bootvga,bus=pcie.0,addr=0x06,vgamem_mb=4,xres=1280,yres=800,xmax=1280,ymax=800,refresh_rate=60000 \
      -device thunderbolt-root-port,id=tbrp0,bus=pcie.0,chassis=1,slot=1,addr=0x05 \
      ...

Hotplug a Thunderbolt VGA endpoint after the guest has booted:

::

  (qemu) device_add thunderbolt-vga,id=tbvga0,bus=tbrp0,addr=0x00
  (qemu) device_del tbvga0

Temporarily disconnect and reconnect the whole Thunderbolt port:

::

  (qemu) qom-set /machine/peripheral/tbrp0 connected false
  (qemu) qom-set /machine/peripheral/tbrp0 connected true

USB storage example
-------------------

Attach a USB controller to the Thunderbolt root port, then attach USB mass
storage to that controller:

::

  (qemu) device_add qemu-xhci,id=tbxhci0,bus=tbrp0,addr=0x00
  (qemu) drive_add 0 if=none,id=TBUSB,file=TBUSB.raw,format=raw,cache=writeback,aio=threads
  (qemu) device_add usb-storage,id=tbusb0,bus=tbxhci0.0,drive=TBUSB,serial=tb-usb-stick0

After the guest ejects or unmounts the disk, remove the USB device and the
controller:

::

  (qemu) device_del tbusb0
  (qemu) device_del tbxhci0

NVMe example
------------

Attach an NVMe controller behind the Thunderbolt root port:

::

  (qemu) drive_add 0 if=none,id=TBNVME,file=TBNVME.qcow2,format=qcow2
  (qemu) device_add nvme,id=tbnvme0,bus=tbrp0,addr=0x00,serial=tb-nvme0,drive=TBNVME
  (qemu) device_del tbnvme0

Notes for review
----------------

The regular QEMU ``VGA`` type is intentionally left non-hotpluggable.  Tests
check that hotplugging ``VGA`` behind a Thunderbolt root port still fails, and
that ``thunderbolt-vga`` can be hotplugged with constrained display defaults.

The macOS validation flow used QCOW2 overlays for all mutable disks so the base
guest images remained unchanged.
