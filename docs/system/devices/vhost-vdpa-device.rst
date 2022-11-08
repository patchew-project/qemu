
=========================
generic vhost-vdpa device
=========================

This document explains the usage of the generic vhost vdpa device.

Description
-----------

vDPA(virtio data path acceleration) device is a device that uses a datapath
which complies with the virtio specifications with vendor specific control
path.

QEMU provides two types of vhost-vdpa devices to enable the vDPA device, one
is type sensitive which means QEMU needs to know the actual device type
(e.g. net, blk, scsi) and another is called "generic vdpa device" which is
type insensitive (likes vfio-pci).

Examples
--------

Prepare the vhost-vdpa backends first:

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
