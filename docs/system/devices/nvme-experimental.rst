===========================
Experimental NVMe Emulation
===========================

QEMU offers experimental NVMe emulation through the ``x-nvme-ctrl`` device and
the ``x-nvme-subsystem`` and ``x-nvme-ns-{nvm,zoned}`` objects.


Adding NVMe Devices
===================

Controller Emulation
--------------------

The QEMU emulated NVMe controller implements version 1.4 of the NVM Express
specification. All mandatory features are implement with a couple of exceptions
and limitations:

  * Accounting numbers in the SMART/Health log page are reset when the device
    is power cycled.
  * Interrupt Coalescing is not supported and is disabled by default.

The simplest way to attach an NVMe controller on the QEMU PCI bus is to add the
following parameters:

.. code-block:: console

    -object x-nvme-subsystem,id=nvme-subsys-0
    -device x-nvme-ctrl,subsys=nvme-subsys-0

There are a number of optional general parameters for the ``x-nvme-ctrl``
device. Some are mentioned here, but see ``-device x-nvme-ctrl,help`` to list
all possible parameters.

``max-ioqpairs=UINT32`` (default: ``64``)
  Set the maximum number of allowed I/O queue pairs.

``msix-vectors=UINT16`` (default: ``65``)
  The number of MSI-X vectors that the device should support.

``mdts=UINT8`` (default: ``7``)
  Set the Maximum Data Transfer Size of the device.


Additional Namespaces
---------------------

The invocation sketched above does not add any namespaces to the subsystem. To
add these, add ``x-nvme-ns-NSTYPE`` (where ``NSTYPE`` is either ``nvm`` or
``zoned``) objects with attached blockdevs and a reference to the subsystem:

.. code-block:: console

    -blockdev file,node-name=blk-file-nvm-1,filename=nvm-1.img
    -blockdev raw,node-name=blk-nvm-1,file=blk-file-nvm-1
    -object x-nvme-ns-nvm,id=nvm-1,blockdev=blk-nvm-1,subsys=nvme-subsys-0

There are a number of optional parameters available (common to both the ``nvm``
and ``zoned`` namespace types):

``nsid`` (default: ``"auto"``)
  Explicitly set the namespace identifier. If left at the default, the
  subsystem will allocate the next available identifier.

``uuid`` (default: ``"auto"``)
  Set the UUID of the namespace. This will be reported as a "Namespace UUID"
  descriptor in the Namespace Identification Descriptor List. If left at the
  default, a UUID will be generated.

``eui64`` (default: ``"auto"``)
  Set the EUI-64 of the namespace. This will be reported as a "IEEE Extended
  Unique Identifier" descriptor in the Namespace Identification Descriptor
  List. If left at the default, an identifier prefixed with the QEMU IEEE OUI
  (``52:54:00``) will be generated.

``lba-size`` (default: ``4096``)
  Set the logical block size.

Namespaces support LBA metadata in the form separate metadata (``MPTR``-based)
and extended LBAs.

``metadata-size`` (default: ``0``)
  Defines the number of metadata bytes per LBA.

``extended-lba`` (default: ``off/false``)
  Set to ``on/true`` to enable extended LBAs.

With metadata configured, namespaces support DIF- and DIX-based protection
information (depending on ``extended-lba``).

``pi-type`` (default: ``"none"``)
  Enable protection information of the specified type (type ``"type1"``,
  ``"type2"`` or ``"type3"``).

``pi-first`` (default: ``off/false``)
  Controls the location of the protection information within the metadata. Set
  to ``on/true`` to transfer protection information as the first eight bytes of
  metadata. Otherwise, the protection information is transferred as the last
  eight bytes.

The ``zoned`` namespace type has additional parameters:

``zone-size`` (default: ``4096``)
  The number of LBAs in a zone.

``zone-capacity`` (default: ``4096``)
  The number of writable LBAs in a zone.
