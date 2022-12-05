
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

1. Please make sure the modules listed bellow are installed:
    vhost.ko
    vhost_iotlb.ko
    vdpa.ko
    vhost_vdpa.ko


2. Prepare the vhost-vDPA backends, here is an example using vdpa_sim_blk
   device:

::
  host# modprobe vdpa_sim_blk
  host# vdpa dev add mgmtdev vdpasim_blk name blk0
  (...you can see the vhost-vDPA device under /dev directory now...)
  host# ls -l /dev/vhost-vdpa-*
  crw------- 1 root root 236, 0 Nov  2 00:49 /dev/vhost-vdpa-0

Note:
It needs some vendor-specific steps to provision the vDPA device if you're
using real HW devices, such as installing the vendor-specific vDPA driver
and binding the device to the driver.


3. Start the virtual machine:

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
