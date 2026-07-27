.. SPDX-License-Identifier: GPL-2.0-or-later
.. _igb-migration:

igb VF Migration
----------------

The igb device supports an experimental VF migration interface that allows
the ``igb-vfio-pci`` variant driver to migrate VF state during live
migration. This is enabled with the ``x-vf-migration`` property::

  -device igb,x-vf-migration=on,...

When enabled, each emulated VF advertises a vendor-specific PCI capability
(cap id 0x09) with a magic signature (``0x4D494742`` / "MIGB") that the
variant driver probes at bind time. The capability contains an interface
version number, the BAR index hosting the migration register region, and
feature flags indicating which migration features are supported.

This feature is experimental and the ``x-`` prefix indicates the interface
may change.

Migration BAR layout
~~~~~~~~~~~~~~~~~~~~

The migration BAR (BAR2, 64 KB) implements a VFIO-like state machine with
the following register layout::

  Offset  Name                Access  Description
  0x000   DEVICE_STATE        RW      Migration state (RUNNING=2, STOP=1,
                                      STOP_COPY=3, RESUMING=4, PRE_COPY=5)
  0x004   STATUS              RO      Flags[2:0]: DATA_AVAIL, ERROR, QUIESCED
                                      Error code[15:8] (when ERROR is set)
  0x008   CAPS                RO      F_STATE, F_DIRTY, max_ranges[11:8],
                                      pgsizes[31:12]
  0x00C   VERSION             RO      Interface version (1)
  0x010   DATA_SIZE           RW      Max state size at reset, actual after save
  0x014   DATA_XFER           WO      Trigger DMA save or DMA load
  0x018   DATA_BUF_ADDR_LO    WO      Low 32 bits of state DMA buffer address
  0x01C   DATA_BUF_ADDR_HI    WO      High 32 bits of state DMA buffer address

State transitions follow the VFIO migration state machine: the driver
writes to ``DEVICE_STATE`` to move between states and reads ``STATUS``
to check for completion.

State data is transferred via a driver-provided DMA buffer. The driver
writes its PF DMA address to ``DATA_BUF_ADDR_LO/HI`` and triggers the
transfer with ``DATA_XFER``. The device DMA-writes the serialized state
on save and DMA-reads it on restore. DMA is performed through the PF
device because VFIO owns the VF's IOMMU domain.

The state blob is a versioned sequence of register (offset, value)
pairs.

When ``STATUS`` has the ``ERROR`` bit set, bits [15:8] contain an error
code identifying the failure::

  0  (none)          No error
  1  BAD_MAGIC       State blob magic mismatch
  2  BAD_VERSION     State blob version mismatch
  3  BAD_SIZE        State blob too large or empty
  4  BAD_VFN         VF number mismatch (source != destination)
  5  DMA_FAILED      DMA transfer to/from state buffer failed
  6  NO_BUFFER       DATA_XFER without buffer address set
