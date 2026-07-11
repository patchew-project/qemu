SPDX-License-Identifier: GPL-2.0-or-later

# QEMU Thunderbolt PCIe hotplug layer

This branch adds a Thunderbolt-flavoured PCIe hotplug layer for QEMU
experiments.  The implementation keeps normal QEMU PCI devices intact and
adds a new Thunderbolt bus/root-port layer around them.

The primary goal is to provide hotplug and hot-unplug for real and virtual
devices in guests that do not support those devices being added or removed
directly.  macOS is the main validation target for this path: devices can be
tunnelled through a guest-visible Thunderbolt layer even when direct PCIe
hotplug for the same endpoint is not usable.

The regular QEMU `VGA` device is not changed and is not made hotpluggable.
Thunderbolt display experiments use a separate `thunderbolt-vga` device type.

## Added device types

- `thunderbolt-root-port`: a PCIe root port that creates a `thunderbolt-bus`.
  Its `connected` QOM property controls guest-visible link presence.
- `thunderbolt-pcie-pci-bridge`: a PCIe-to-PCI bridge for tunnelled PCI
  endpoints with Thunderbolt-related ACPI properties.
- `thunderbolt-vga`: a hotpluggable stdvga-compatible endpoint with constrained
  default EDID settings for guest hotplug tests.

## Basic boot example

Start the guest with its normal boot display and an empty Thunderbolt root
port:

```sh
qemu-system-x86_64 \
  -machine q35 \
  -device VGA,id=bootvga,bus=pcie.0,addr=0x06,vgamem_mb=4,xres=1280,yres=800,xmax=1280,ymax=800,refresh_rate=60000 \
  -device thunderbolt-root-port,id=tbrp0,bus=pcie.0,chassis=1,slot=1,addr=0x05 \
  ...
```

Hotplug and remove a Thunderbolt VGA endpoint through the QEMU monitor:

```text
(qemu) device_add thunderbolt-vga,id=tbvga0,bus=tbrp0,addr=0x00
(qemu) device_del tbvga0
```

Disconnect or reconnect the whole Thunderbolt root port:

```text
(qemu) qom-set /machine/peripheral/tbrp0 connected false
(qemu) qom-set /machine/peripheral/tbrp0 connected true
```

## USB storage example

Attach a USB controller behind the Thunderbolt root port, then attach USB mass
storage to that controller:

```text
(qemu) device_add qemu-xhci,id=tbxhci0,bus=tbrp0,addr=0x00
(qemu) drive_add 0 if=none,id=TBUSB,file=TBUSB.raw,format=raw,cache=writeback,aio=threads
(qemu) device_add usb-storage,id=tbusb0,bus=tbxhci0.0,drive=TBUSB,serial=tb-usb-stick0
```

After the guest ejects or unmounts the disk, remove the device and controller:

```text
(qemu) device_del tbusb0
(qemu) device_del tbxhci0
```

## NVMe example

```text
(qemu) drive_add 0 if=none,id=TBNVME,file=TBNVME.qcow2,format=qcow2
(qemu) device_add nvme,id=tbnvme0,bus=tbrp0,addr=0x00,serial=tb-nvme0,drive=TBNVME
(qemu) device_del tbnvme0
```

## Validation status

The development validation used macOS guests booted from overlay disk images so
the base images stayed immutable.  The tested hotplug flows covered:

- `thunderbolt-vga` detection and removal while the standard boot VGA remained.
- NVMe controller hotplug and driver instance disappearance after unplug.
- USB controller hotplug, USB storage mount/write/eject, controller removal,
  replug, and data persistence verification.

The qtest coverage checks that regular `VGA` hotplug still fails while
`thunderbolt-vga` can be hotplugged behind `thunderbolt-root-port`.
