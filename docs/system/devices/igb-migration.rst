.. SPDX-License-Identifier: GPL-2.0-or-later
.. _igb-migration:

igb VF Migration
----------------

Live migration of VFIO-passthrough devices (SR-IOV VFs, vGPUs) is a
growing requirement, but real hardware with migration support is scarce
and hard to debug. An emulated device provides a fully controlled
testbed for developing and validating the entire software stack --
vfio-pci variant drivers, VFIO core migration v2 framework, QEMU,
libvirt -- and for tuning complex migration policies such as downtime
convergence. It also serves as an educational reference for
understanding VFIO migration end-to-end, from device state
serialization to dirty page tracking.

The igb device supports an experimental VF migration interface that allows
the `igb-vfio-pci`_ variant driver to migrate VF state during live
migration using the standard VFIO migration v2 protocol with stop-copy
and pre-copy support.

This is enabled with the ``x-vf-migration`` property::

  -device igb,x-vf-migration=on,...

Each emulated VF then advertises a DVSEC discovered by the
`igb-vfio-pci`_ variant driver at bind time. This feature is
experimental (``x-`` prefix, default off).

Architecture
~~~~~~~~~~~~

The target scenario is nested virtualization::

  L0 QEMU
    igb PF with x-vf-migration=on
    └── VFs with migration DVSEC

  L1 kernel
    igb-vfio-pci variant driver
    translates VFIO migration v2 ioctls → DVSEC config writes

  L1 QEMU (stock, unmodified)
    vfio-pci device model, standard migration fd

  L2 guest
    standard igbvf driver, unaware of migration

The L1 QEMU is completely unmodified -- it sees a standard VFIO
migratable device and uses the normal migration fd path. The
`igb-vfio-pci`_ variant driver handles the translation between
VFIO migration v2 ioctls and DVSEC config writes.

Design
~~~~~~

The migration interface is exposed through a DVSEC at offset 0x160
in VF extended config space (see `DVSEC register layout`_ below for
the full register map).

Device state is serialized as a versioned blob of per-VF register
(offset, value) pairs covering control, interrupt, RX/TX queue,
receive address (RA/RA2), etc. plus TX context descriptors and
VFRE/VFTE enable bits. The buffer address is a guest physical address
(GPA) written by the driver via ``virt_to_phys``; the device accesses
guest RAM directly through the system address space.

Dirty page tracking is implemented with per-range bitmaps maintained
in IGBCore. All VF DMA paths in ``igb_core.c`` (TX data, RX data,
descriptor writeback) are instrumented to record touched pages. The
`igb-vfio-pci`_ variant driver registers tracked IOVA ranges and
queries dirty bitmaps through a shared buffer. Buffer structures
include len, flags, and reserved fields for future extensibility.

The dirty bitmaps are maintained inside the device, which is not
realistic for discrete NICs without on-chip DRAM.

DVSEC register layout
~~~~~~~~~~~~~~~~~~~~~

The migration DVSEC (36 bytes at offset ``0x160``) uses a command
doorbell model. All commands are synchronous -- the device completes
the operation before the config write returns::

  Offset  Name          Access  Description
  +0x00   ExtCap Hdr    RO      PCIe extended cap (id=0x23, ver=1)
  +0x04   DVSEC Hdr 1   RO      length[31:20] | rev[19:16] | vendor_id[15:0]
  +0x08   DVSEC Hdr 2   RO      DVSEC ID (1)
  +0x0A   Reserved      -       Padding for DWORD alignment
  +0x0C   CAPS          RO      F_STATE[0], F_DIRTY[1], max_ranges[11:8], pgsize[16:12]
  +0x10   CTRL          WO      Doorbell: cmd[7:0], arg[31:8]
  +0x14   STATUS        RO      state[7:0], error_code[15:8], quiesced[16]
  +0x18   BUF_ADDR_LO   RW      Shared DMA buffer GPA (low 32 bits)
  +0x1C   BUF_ADDR_HI   RW      Shared DMA buffer GPA (high 32 bits)
  +0x20   DATA_SIZE     RO      State blob size in bytes

CTRL commands::

  Cmd  Name            Arg             Description
  1    SET_STATE       state[31:8]     Set migration state
  2    SAVE            -               DMA-write state to buffer
  3    LOAD            size[31:8]      DMA-read state from buffer
  4    DIRTY_ENABLE    -               Enable dirty tracking (params in DMA buffer)
  5    DIRTY_DISABLE   -               Disable dirty tracking
  6    DIRTY_QUERY     -               Query dirty bitmap (via DMA buffer)
  7    GET_STATS       -               Query statistics (via DMA buffer)

The driver sets ``BUF_ADDR_LO/HI`` before issuing commands that use a
DMA buffer (SAVE, LOAD, DIRTY_ENABLE, DIRTY_QUERY, GET_STATS). The
buffer address is latched per CTRL write, so the driver can use
different buffers for different commands. The buffer address is a
guest physical address (GPA).

State transitions follow the VFIO migration v2 state machine. The
driver issues ``SET_STATE`` with the target state in the arg field and
reads ``STATUS`` to confirm the transition. Device states::

  0  ERROR       1  STOP       2  RUNNING
  3  STOP_COPY   4  RESUMING   5  PRE_COPY

``DATA_SIZE`` reflects the state blob size. At reset and in ``STOP``
state it holds the maximum size the driver should allocate. After
``SET_STATE(STOP_COPY)`` or ``SAVE`` it holds the actual serialized
size. The driver reads it after entering ``STOP_COPY`` to allocate an
exact-sized DMA buffer before issuing ``SAVE``.

The state blob is a versioned sequence of register (offset, value)
pairs with magic ``0x4D494742`` ("MIGB").

When ``STATUS`` state is ``ERROR`` (0), bits [15:8] contain an error
code identifying the failure::

  1   UNK_CMD           Unknown CTRL command
  2   BAD_STATE         Command issued in wrong migration state
  3   NO_BUFFER         Command requires buffer but BUF_ADDR not set
  4   DMA_FAILED        DMA transfer to/from buffer failed
  5   BAD_SIZE          State blob too large or empty
  6   BAD_MAGIC         State blob magic mismatch
  7   BAD_VERSION       State blob version mismatch
  8   TOO_MANY_RANGES   Exceeds max_ranges from CAPS
  9   BAD_RANGE         Invalid range (zero size, misaligned, not contained)
  10  BAD_PGSIZE        Invalid or misaligned page size
  11  NOT_ENABLED       Dirty query without prior enable

Dirty page tracking
~~~~~~~~~~~~~~~~~~~

The migration interface supports per-VF dirty page tracking, advertised
by the ``F_DIRTY`` flag (bit 1) in ``CAPS``. This allows the variant
driver to enter ``PRE_COPY`` state while the VM continues to run,
iterating on dirty pages to reduce the final stop-and-copy window.

The device maintains one dirty tracking engine per range, each with its
own bitmap scoped to the range boundaries. The ``CAPS`` register
advertises the maximum number of ranges in bits [11:8].

Dirty tracking is controlled through CTRL commands:

- **DIRTY_ENABLE** (4): the driver fills an ``igb_mig_dirty_enable_req``
  struct in the DMA buffer with the page size, range IOVA, and range
  size, then issues the command. The device allocates a bitmap for the
  range and begins recording pages touched by DMA. Supported page sizes
  are advertised in ``CAPS`` bits [16:12] (bit N = 2^N bytes). The driver
  checks ``STATUS`` for errors after the command completes.
- **DIRTY_DISABLE** (5): tears down all ranges and stops tracking.
- **DIRTY_QUERY** (6): the driver writes (iova, size) into the
  ``igb_mig_dirty_query`` DMA buffer, then issues the command. The
  device validates the range, copies the dirty bitmap into the buffer,
  and clears the tracked bits after successful DMA. The driver checks
  ``STATUS`` for errors after the command.

Dirty enable DMA buffer
~~~~~~~~~~~~~~~~~~~~~~~

The ``DIRTY_ENABLE`` command reads its parameters from the DMA buffer.
The ``len`` field holds the total structure size (including reserved
bytes) so the device can detect newer formats. ``flags`` and
``reserved`` must be zero::

  Offset  Field        Type      Description
  0x00    len          uint32    Structure size in bytes
  0x04    flags        uint32    Reserved, must be 0
  0x08    pgsize       uint64    Page granularity (must match a CAPS pgsize bit)
  0x10    range_iova   uint64    Tracked range start address
  0x18    range_size   uint64    Tracked range size in bytes
  0x20    reserved[4]  uint32    Reserved, must be 0

Dirty query DMA buffer
~~~~~~~~~~~~~~~~~~~~~~

The ``DIRTY_QUERY`` command uses a shared DMA buffer for both request
and response. The ``len`` field holds the total buffer size (header +
bitmap). ``flags`` and ``reserved`` must be zero::

  Offset  Field              Written by  Description
  0x00    len                driver      Total buffer size in bytes
  0x04    flags              driver      Reserved, must be 0
  0x08    iova               driver      Query range start
  0x10    size               driver      Query range size
  0x18    bitmap_size        device      Bytes written to bitmap
  0x1C    dirty_page_count   device      Number of set bits
  0x20    dma_writes         device      DMA write count (diagnostic)
  0x28    reserved[6]        -           Reserved, must be 0
  0x40    bitmap[]           device      Dirty page bitmap

Migration statistics
~~~~~~~~~~~~~~~~~~~~

The ``GET_STATS`` command DMA-writes a statistics response into the
driver-provided buffer. The driver sets ``BUF_ADDR_LO/HI`` and issues
the command; the device writes the response and returns::

  Offset  Field                Type      Description
  0x00    dma_writes           uint64    DMA write operations tracked
  0x08    dma_bytes            uint64    DMA bytes written
  0x10    dirty_pages_set      uint32    Dirty pages marked since enable
  0x14    dirty_pages_cleared  uint32    Dirty pages cleared by queries
  0x18    dirty_page_count     uint32    Current dirty pages (set - cleared)
  0x1C    dirty_query_count    uint32    Number of QUERY operations

The variant driver exposes these via debugfs at
``/sys/kernel/debug/vfio/<device>/migration/dirty/stats``.

Testing setup
~~~~~~~~~~~~~

The target scenario is nested virtualization: L0 runs QEMU with an
igb PF (``x-vf-migration=on``), L1 runs the `igb-vfio-pci`_ variant
driver and an unmodified QEMU, and L2 runs a standard igbvf driver.
See `Architecture`_ above for the full stack diagram.

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

Migration under iperf3 load works correctly: dirty page tracking
converges (from ~2000 pages per PRE_COPY iteration down to ~280 at
STOP_COPY), and STOP_COPY stays under 250ms.

Todo
~~~~

1. Add migration blocker when ``x-vf-migration=on`` (no VMState yet) or
   add VMState support for L0 migration (dirty bitmaps, tracking
   engines, DVSEC registers, stats)
2. Add PRE_COPY state transfer to validate device INIT data (magic,
   version, etc.)
3. Add qtests for migration state machine transitions, dirty page
   tracking

Ideas
~~~~~

1. **RX bandwidth throttle** (``x-mig-rx-limit``, uint32, default 0)

   Return false from ``can_receive`` when the per-VF packet count in the
   current tracking interval exceeds the limit. Reduces DMA writes and
   dirty pages realistically.

2. **Migration phase timing** (GET_STATS extension)

   Add per-VF timestamps: ``precopy_start_ns``, ``stopcopy_start_ns``,
   ``precopy_duration_ns``, ``stopcopy_duration_ns``,
   ``state_transition_count``. Expose via GET_STATS.

3. **Hot page simulation** (``x-mig-hot-pages``, uint32, default 0)

   Re-set the first N bitmap bits after each DIRTY_QUERY, simulating
   workloads with hot pages that prevent convergence.

4. **Error injection** (``x-mig-inject-error``, uint32, default 0)

   One-shot error code injection before command dispatch. A separate
   ``x-mig-inject-dma-fail`` (bool) for persistent DMA failure testing.

AI disclaimer
~~~~~~~~~~~~~

Claude was used to analyze the IGB PF and VF internal state and
identify the pain points of a working live migration of such devices.
The generated code served as a starting point but *significant* time
was then spent cleaning up, reworking, and shaping it into a clear,
reviewable IGB model extension.

.. _igb-vfio-pci: https://github.com/legoater/vfio-pci-extras
