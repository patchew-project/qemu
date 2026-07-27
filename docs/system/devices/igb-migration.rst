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
  0x020   DIRTY_PGSIZE        RW      Dirty tracking page granularity
  0x024   DIRTY_CTRL          WO      0=DISABLE, 1=ENABLE, 2=QUERY
  0x028   DIRTY_RANGE_IOVA_LO WO      Low 32 bits of tracked range start
  0x02C   DIRTY_RANGE_IOVA_HI WO      High 32 bits of tracked range start
  0x030   DIRTY_RANGE_SIZE    WO      Tracked range size in bytes
  0x034   DIRTY_BUF_ADDR_LO   WO      Low 32 bits of shared buffer address
  0x038   DIRTY_BUF_ADDR_HI   WO      High 32 bits of shared buffer address
  0x03C   DIRTY_STATUS        RO      Result of last DIRTY_CTRL (0=OK, 1-5=error)

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

Dirty page tracking
~~~~~~~~~~~~~~~~~~~

The migration interface supports per-VF dirty page tracking, advertised
by the ``F_DIRTY`` flag in ``CAPS``. This allows the variant driver to
enter ``PRE_COPY`` state (``DEVICE_STATE`` = 5) while the VM continues
to run, iterating on dirty pages to reduce the final stop-and-copy
window.

The device maintains one dirty tracking engine per range, each with its
own bitmap scoped to the range boundaries. The ``CAPS`` register
advertises the maximum number of ranges (``max_ranges`` in bits [11:8])
and supported page sizes (bits [31:12]).

Dirty tracking is controlled through the ``DIRTY_CTRL`` register:

- **ENABLE** (1): the driver programs a tracked range via
  ``DIRTY_RANGE_IOVA_LO/HI`` + ``DIRTY_RANGE_SIZE`` then writes
  ``DIRTY_CTRL=ENABLE``. The device allocates a fixed-size bitmap for
  the range and begins recording pages touched by DMA (TX data, RX
  data, descriptor writeback). The page granularity is set by
  ``DIRTY_PGSIZE`` (default 4096). After each ENABLE the driver reads
  ``DIRTY_STATUS`` to check for errors.
- **DISABLE** (0): tears down all ranges and stops tracking.
- **QUERY** (2): the driver writes (IOVA, size, page_size) into a
  shared buffer registered via ``DIRTY_BUF_ADDR_LO/HI`` (PF DMA
  address, as for state transfers), then writes
  ``DIRTY_CTRL=QUERY``. The device finds the matching range, copies
  the dirty bitmap into the shared buffer, clears the tracked bits,
  and sets the buffer's completion status.

``DIRTY_STATUS`` values after each ``DIRTY_CTRL`` write::

  0  OK               Success
  1  TOO_MANY_RANGES  Exceeds max_ranges from CAPS
  2  BAD_RANGE        Invalid range (zero size)
  3  BAD_PGSIZE       Invalid or misaligned page size
  4  NOT_ENABLED      Query without prior enable
  5  NO_BUFFER        Query without shared buffer

Dirty query shared buffer
~~~~~~~~~~~~~~~~~~~~~~~~~

The shared buffer used for dirty queries is registered via
``DIRTY_BUF_ADDR_LO/HI`` (PF DMA address). It is cache-line aligned
(64 bytes) to separate driver-written request fields from
device-written completion fields::

  Offset  Field              Written by  Description
  0x00    iova               driver      Query range start
  0x08    size               driver      Query range size
  0x10    page_size          driver      Page granularity
  0x14    flags              driver      Reserved (must be 0)
  0x18    reserved[10]       -           Pad to 64-byte cache line
  0x40    status             device      0 = pending, 1 = complete
  0x44    bitmap_size        device      Bytes written to bitmap
  0x48    dirty_page_count   device      Number of set bits in bitmap
  0x4C    reserved[12]       -           Pad to 64-byte cache line
  0x80    bitmap[]           device      Dirty page bitmap

The driver fills the request fields, issues ``DIRTY_CTRL=QUERY``, and
polls ``status`` for completion. The device reads the request, writes
the dirty bitmap and completion fields via DMA, then sets
``status = 1``.

Testing setup
~~~~~~~~~~~~~

The target scenario is nested virtualization::

  L0 QEMU
    igb PF with x-vf-migration=on
    └── VFs with migration BAR + vendor cap

  L1 kernel
    igb-vfio-pci variant driver
    translates VFIO migration v2 ioctls → BAR2 MMIO

  L1 QEMU (stock, unmodified)
    vfio-pci device model, standard migration fd

  L2 guest
    standard igbvf driver, unaware of migration

NetworkManager configuration
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In a nested setup, the L1 VMs (source and destination) have emulated
igb PFs connected to the L0 bridge. By default, NetworkManager
acquires DHCP leases on those PF interfaces and on any igb VFs created
later. This causes the VF MAC address to be learned on the L0 bridge,
which can misdirect iperf3 traffic after migration.

To prevent this, configure NetworkManager on **both L1 VMs** and on the
**L2 guest disk image**.

L1 VMs (source and destination)
...............................

1. Prevent NetworkManager from managing igbvf interfaces:

.. code-block:: bash

   cat > /etc/NetworkManager/conf.d/99-no-igbvf.conf <<EOF
   [keyfile]
   unmanaged-devices=driver:igbvf
   EOF

2. Disable IP on the igb PF connections (keep the interfaces UP for
   bridging, but with no DHCP lease):

.. code-block:: bash

   # Identify the NM connections for the igb PFs (NOT the virtio management NIC)
   nmcli -t -f NAME,DEVICE connection show

   # For each igb PF connection:
   nmcli connection modify "<igb-pf-connection>" ipv4.method disabled ipv6.method disabled

3. Reload NetworkManager:

.. code-block:: bash

   nmcli general reload

L2 guest disk image
...................

Use ``virt-customize`` to add the igbvf unmanaged config to the guest
image (offline, before any test run):

.. code-block:: bash

   virt-customize -a /srv/migration/rhel10.qcow2 \
     --write /etc/NetworkManager/conf.d/99-no-igbvf.conf:'[keyfile]
   unmanaged-devices=driver:igbvf'

Network diagram
^^^^^^^^^^^^^^^

The diagram below shows the nested setup where the source and
destination hosts are themselves VMs (L1) running on a physical
host (L0) that emulates the igb NIC::

  ┌────────────────────────────────────────────────────────────────────────────┐
  │  L0: physical host                                                         │
  │                                                                            │
  │  virbr0  192.168.199.1/24                                                  │
  │  ├── NFS server: /srv/migration                                            │
  │  └── iperf3 client: iperf3 -c 192.168.199.200 -t 60 -i 1                   │
  │      │                                                                     │
  │      │  L0 virbr0 bridge (192.168.199.0/24)                                │
  │  ────┼──────────┬──────────────────┬──────────────────────────────         │
  │      │          │                  │                                       │
  │      │     ┌────┴────┐        ┌────┴────┐                                  │
  │      │     │ virtio  │        │ emulated│                                  │
  │      │     │c0:ff:ee:│        │  igb PF │    L0 QEMU (vm6)                 │
  │      │     │ :00:06  │        │ + igbvf │    tracks DMA dirty pages        │
  │      │     └────┬────┘        └────┬────┘                                  │
  │      │          │                  │                                       │
  │  ┌───┼──────────┼──────────────────┼──────────────────────────────────┐    │
  │  │   │  L1: vm6 (source)           │                                  │    │
  │  │   │  enp1s0: 192.168.199.6      │                                  │    │
  │  │   │  (management)               │                                  │    │
  │  │   │                        enp8s0 (igb PF, no IP)                  │    │
  │  │   │                             │                                  │    │
  │  │   │                        igb VF0 ──► igb-vfio-pci (VFIO)         │    │
  │  │   │                             │      dirty_sync → L0 igbvf       │    │
  │  │   │                             │                                  │    │
  │  │   │   virbr0                    │ VFIO passthrough                 │    │
  │  │   │   192.168.200.1/24          │                                  │    │
  │  │   │       │                     │                                  │    │
  │  │   │  ┌────┼─────────────────────┼───────────────────────────┐      │    │
  │  │   │  │    │  L2: rhel10 guest   │                           │      │    │
  │  │   │  │    │                     │                           │      │    │
  │  │   │  │  virtio NIC           igb VF (enp7s0)                │      │    │
  │  │   │  │  192.168.200.130/24   192.168.199.200/24             │      │    │
  │  │   │  │  (SSH login)          (iperf3 data path)             │      │    │
  │  │   │  │                          │                           │      │    │
  │  │   │  │            iperf3 -s -D  │ (listens on 0.0.0.0)      │      │    │
  │  │   │  └──────────────────────────┼───────────────────────────┘      │    │
  │  │   │                             │                                  │    │
  │  │   │  virsh migrate --live ──────┼──────────────────► vm7           │    │
  │  │   │                             │                                  │    │
  │  └───┼─────────────────────────────┼──────────────────────────────────┘    │
  │      │                             │                                       │
  │      │          iperf3 traffic     │                                       │
  │      └─────────────────────────────┘                                       │
  │                                                                            │
  │  ────────────────────────────────────────────────────────────────          │
  │      │                  │                                                  │
  │      │     ┌────────┐   │   ┌─────────┐                                    │
  │      │     │ virtio │   │   │emulated │    L0 QEMU (vm7)                   │
  │      │     │c0:ff:ee│   │   │ igb PF  │                                    │
  │      │     │ :00:07 │   │   │ + igbvf │                                    │
  │      │     └────┬───┘   │   └────┬────┘                                    │
  │  ┌──────────────┼───────┼────────┼────────────────────────────────────┐    │
  │  │   L1: vm7 (destination)       │                                    │    │
  │  │   enp1s0: 192.168.199.7       │                                    │    │
  │  │   (management)           enp8s0 (igb PF, no IP)                    │    │
  │  │                               │                                    │    │
  │  │                          igb VF0 ──► igb-vfio-pci (VFIO)           │    │
  │  │                               │                                    │    │
  │  │   virbr0                      │ VFIO passthrough                   │    │
  │  │   192.168.200.1/24            │                                    │    │
  │  │       │                       │                                    │    │
  │  │  ┌────┼───────────────────────┼────────────────────────────┐       │    │
  │  │  │    │  L2: rhel10 (after migration)                      │       │    │
  │  │  │    │                       │                            │       │    │
  │  │  │  virtio NIC             igb VF (enp7s0)                 │       │    │
  │  │  │  192.168.200.130/24     192.168.199.200/24              │       │    │
  │  │  │                            │                            │       │    │
  │  │  │              iperf3 -s -D  │ (connection survives)      │       │    │
  │  │  └────────────────────────────┼────────────────────────────┘       │    │
  │  └───────────────────────────────┼────────────────────────────────────┘    │
  │                                  │                                         │
  │      iperf3 traffic resumes ─────┘                                         │
  │      (same IP, same MAC, same L2 segment → transparent to client)          │
  └────────────────────────────────────────────────────────────────────────────┘
