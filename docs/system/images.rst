.. _disk images:

Disk Images
-----------

QEMU supports many different types of storage protocols, disk image file
formats, and filter block drivers. *Protocols* provide access to storage such
as local files or NBD exports. *Formats* implement file formats that are useful
for sharing disk image files and add functionality like snapshots. *Filters*
add behavior like I/O throttling.

These features are composable in a graph. Each graph node is called a
*blockdev*. This makes it possible to construct many different storage
configurations. The simplest example is accessing a raw image file::

   --blockdev file,filename=test.img,node-name=drive0

A qcow2 image file throttled to 10 MB/s is specified like this::

   --object throttle-group,x-bps-total=10485760,id=tg0 \
   --blockdev file,filename=vm.qcow2,node-name=file0 \
   --blockdev qcow2,file=file0,node-name=qcow0 \
   --blockdev throttle,file=qcow0,throttle-group=tg0,node-name=drive0

Blockdevs are not directly visible to guests. Guests use emulated storage
controllers like a virtio-blk device to access a blockdev::

   --device virtio-blk-pci,drive=drive0

Note that QEMU has an older ``--drive`` syntax that is somewhat similar to
``--blockdev``. ``--blockdev`` is preferred because ``--drive`` mixes storage
controller and blockdev definitions in a single option that cannot express
everything. When a "drive" or "device" is required by a command-line option or
QMP command, a blockdev node-name can be used.

The remainder of this chapter covers the block drivers and how to work with
disk images.

.. _disk_005fimages_005fquickstart:

Quick start for disk image creation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You can create a disk image with the command::

   qemu-img create myimage.img mysize

where myimage.img is the disk image filename and mysize is its size in
kilobytes. You can add an ``M`` suffix to give the size in megabytes and
a ``G`` suffix for gigabytes.

See the ``qemu-img`` invocation documentation for more information.

.. _disk_005fimages_005fsnapshot_005fmode:

Snapshot mode
~~~~~~~~~~~~~

If you use the option ``-snapshot``, all disk images are considered as
read only. When sectors in written, they are written in a temporary file
created in ``/tmp``. You can however force the write back to the raw
disk images by using the ``commit`` monitor command (or C-a s in the
serial console).

.. _vm_005fsnapshots:

VM snapshots
~~~~~~~~~~~~

VM snapshots are snapshots of the complete virtual machine including CPU
state, RAM, device state and the content of all the writable disks. In
order to use VM snapshots, you must have at least one non removable and
writable block device using the ``qcow2`` disk image format. Normally
this device is the first virtual hard drive.

Use the monitor command ``savevm`` to create a new VM snapshot or
replace an existing one. A human readable name can be assigned to each
snapshot in addition to its numerical ID.

Use ``loadvm`` to restore a VM snapshot and ``delvm`` to remove a VM
snapshot. ``info snapshots`` lists the available snapshots with their
associated information::

   (qemu) info snapshots
   Snapshot devices: hda
   Snapshot list (from hda):
   ID        TAG                 VM SIZE                DATE       VM CLOCK
   1         start                   41M 2006-08-06 12:38:02   00:00:14.954
   2                                 40M 2006-08-06 12:43:29   00:00:18.633
   3         msys                    40M 2006-08-06 12:44:04   00:00:23.514

A VM snapshot is made of a VM state info (its size is shown in
``info snapshots``) and a snapshot of every writable disk image. The VM
state info is stored in the first ``qcow2`` non removable and writable
block device. The disk image snapshots are stored in every disk image.
The size of a snapshot in a disk image is difficult to evaluate and is
not shown by ``info snapshots`` because the associated disk sectors are
shared among all the snapshots to save disk space (otherwise each
snapshot would need a full copy of all the disk images).

When using the (unrelated) ``-snapshot`` option
(:ref:`disk_005fimages_005fsnapshot_005fmode`),
you can always make VM snapshots, but they are deleted as soon as you
exit QEMU.

VM snapshots currently have the following known limitations:

-  They cannot cope with removable devices if they are removed or
   inserted after a snapshot is done.

-  A few device drivers still have incomplete snapshot support so their
   state is not saved or restored properly (in particular USB).

.. include:: qemu-block-drivers.rst.inc
