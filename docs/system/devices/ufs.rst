.. SPDX-License-Identifier: GPL-2.0-or-later

=============
UFS Emulation
=============

QEMU emulates a Universal Flash Storage (UFS) host controller and the
logical units behind it. The host controller follows the UFSHCI
register interface and reports UFS version 4.1 to the guest.

Two devices are involved:

``ufs``
  The host controller on the PCI bus. A ``sysbus-ufs`` variant exists
  for boards that place the controller on the system bus; it cannot be
  created with ``-device``.

``ufs-lu``
  A logical unit attached to a host controller through ``bus=``. Each
  logical unit is backed by a block device.

The well-known logical units (REPORT LUNS, UFS Device, Boot and RPMB)
are created by the host controller and need no configuration.

Adding a UFS Device
===================

The following adds a host controller with MCQ enabled and one logical
unit backed by ``ufs.img``:

.. code-block:: console

    -drive file=ufs.img,if=none,id=lu0,format=raw \
    -device ufs,id=ufs0,serial=0001,mcq=on \
    -device ufs-lu,drive=lu0,bus=ufs0,lun=0

Add more ``ufs-lu`` devices with different ``lun`` values to create
more logical units. On top of the architecture defconfig, a Linux
guest needs ``CONFIG_SCSI_UFSHCD``, ``CONFIG_SCSI_UFSHCD_PCI`` and
``CONFIG_BLK_DEV_SD``; each logical unit then appears as a SCSI disk.

Host Controller Properties
==========================

``serial``
  Serial number string. It is accepted but not reported to the guest
  yet; the serial number string descriptor reads back empty.

``nutrs`` (default 32, maximum 32)
  Number of UTP transfer request slots.

``nutmrs`` (default 8, maximum 8)
  Number of UTP task management request slots advertised to the guest.

``mcq`` (default off)
  Advertise Multi-Circular Queue (MCQ) support. When off, the guest
  uses the single doorbell transfer request list.

``mcq-maxq`` (default 2, maximum 31)
  Number of MCQ submission and completion queue pairs.

``wb-max-size``, ``wb-min-size`` (default 0x400 and 0x100)
  Maximum and minimum WriteBooster buffer size, in allocation units.
  ``wb-min-size`` must not exceed ``wb-max-size``.

Logical Unit Properties
=======================

``drive``
  Backing block device.

``lun`` (default 0)
  Logical unit number, 0 to 31. A host controller accepts up to 32
  logical units.

Zoned Logical Units
===================

A logical unit on a host-managed zoned drive is a zoned logical unit: a
host-managed zoned block device (ZBC) with the zones of the drive. The
drive is usually a qcow2 image created with the ``zone.*`` options. It
must be writable, and its size and zone size must be multiples of the 4
KiB logical block size. Every sequential zone must be writable to its
end, since gap zones, with which ZBC describes a smaller capacity, are
not modelled. ZBC cannot report an active zone limit, so the drive must
have none below its number of sequential zones. A zoned host block
device (``host_device``) reports zone errors of writes other than the
zone resource errors as I/O errors, so the guest sees them as generic
errors such as ABORTED COMMAND. A logical unit on a host-aware drive is
a regular logical unit.

The following creates an image with 64 MiB zones and adds a regular
logical unit and a zoned one:

.. code-block:: console

    qemu-img create -f qcow2 lu1.qcow2 -o size=4G \
        -o zone.mode=host-managed,zone.size=64M,zone.capacity=64M \
        -o zone.max_open_zones=6

    -drive file=lu0.img,if=none,id=lu0,format=raw \
    -blockdev qcow2,node-name=lu1,file.driver=file,file.filename=lu1.qcow2 \
    -device ufs,id=ufs0,serial=0001 \
    -device ufs-lu,drive=lu0,bus=ufs0,lun=0 \
    -device ufs-lu,drive=lu1,bus=ufs0,lun=1

A Linux guest needs ``CONFIG_BLK_DEV_ZONED``. f2fs keeps its metadata on
a regular logical unit, for example ``mkfs.f2fs -m -c /dev/sdb /dev/sda``
with the zoned logical unit as ``/dev/sdb``, and needs at least as many
open zones as its active logs, 6 by default.

The zoned logical unit reports the ZBC device type in its INQUIRY data,
the open zone limit of the image in the ZBC device characteristics VPD
page, and RC BASIS 1 in READ CAPACITY(16). It handles REPORT ZONES and
the OPEN, CLOSE, FINISH and RESET WRITE POINTER zone operations. A zone
operation acts on one zone or, with the ALL bit, on every zone. A ZONE
COUNT above 1 is rejected. Writes go to the image, which checks them
against its zones. With ``detect-zeroes`` on, the image takes a write of
zeros without moving the write pointer, so keep it off, the default. The
logical unit does not support WRITE SAME. All other commands, including
reads, MODE SENSE and SYNCHRONIZE CACHE, behave as on a regular logical
unit.

The image keeps the zones across restarts.

Limitations
===========

* UniPro and M-PHY are not modelled. Only the UIC commands needed for
  link startup, DME attribute access and hibernation are handled; DME
  reads return fixed values, and DME writes, power mode changes and
  hibernation do not change any link state.
* Only the ``QUERY TASK`` and ``ABORT TASK`` UTP task management
  requests are supported; other task management functions such as
  ``LOGICAL UNIT RESET`` return ``TASK MANAGEMENT FUNCTION NOT
  SUPPORTED``.
* The Boot and RPMB well-known logical units only answer basic SCSI
  commands such as INQUIRY; boot and RPMB data access are not emulated.
* The device cannot be migrated.
