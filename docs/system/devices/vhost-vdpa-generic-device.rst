
=========================
vhost-vDPA generic device
=========================

This document explains the usage of the vhost-vDPA generic device.

Description
-----------

vDPA(virtio data path acceleration) device is a device that uses a datapath
which complies with the virtio specifications with vendor specific control
path.

QEMU provides two types of vhost-vDPA devices to enable the vDPA device, one
is type sensitive which means QEMU needs to know the actual device type
(e.g. net, blk, scsi) and another is called "vhost-vDPA generic device" which
is type insensitive.

The vhost-vDPA generic device builds on the vhost-vdpa subsystem and virtio
subsystem. It is quite small, but it can support any type of virtio device.

Examples
--------

Prepare the vhost-vDPA backends first:

::
  host# ls -l /dev/vhost-vdpa-*
  crw------- 1 root root 236, 0 Nov  2 00:49 /dev/vhost-vdpa-0

Start QEMU with virtio-mmio bus:

::
  host# qemu-system                                                  \
      -M microvm -m 512 -smp 2 -kernel ... -initrd ...               \
      -device vhost-vdpa-device,vhostdev=/dev/vhost-vdpa-0           \
      ...

Start QEMU with virtio-pci bus:

::
  host# qemu-system                                                  \
      -M pc -m 512 -smp 2                                            \
      -device vhost-vdpa-device-pci,vhostdev=/dev/vhost-vdpa-0       \
      ...
