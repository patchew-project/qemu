.. SPDX-License-Identifier: GPL-2.0-or-later
.. _vfio-apple:

======================
vfio-apple (macOS/ARM)
======================

QEMU supports PCI device passthrough on Apple Silicon Macs using a macOS
DriverKit extension (dext) as the host backend.  Unlike Linux VFIO, which
relies on kernel-managed IOMMU groups and ``/dev/vfio``, the Apple backend
communicates with a userspace driver extension through IOKit's
``IOUserClient`` interface.

Requirements
============

- Apple Silicon Mac running macOS
- A DriverKit extension (``VFIOUserPCIDriver``) installed and running for
  the target PCI device
- QEMU built with ``--enable-hvf`` on a Darwin host
- A guest kernel module that speaks the ``apple-dma-pci`` protocol (see
  below)

Usage
=====

Specify the host PCI device by its bus/device/function address:

.. code-block:: console

  -device vfio-apple-pci,host=01:00.0

This creates a ``vfio-apple-pci`` device that connects to the dext instance
managing the given host PCI BDF.  A companion ``apple-dma-pci`` device is
automatically created on the same bus.

Architecture
============

The implementation consists of several components:

``vfio-apple-pci``
  A QOM subclass of ``vfio-pci`` that overrides realize/exit to set up the
  Apple-specific IOMMU container and interrupt delivery.

``vfio-iommu-apple``
  An IOMMU container backend.  DMA map/unmap through the container are
  no-ops because DMA is managed separately through the companion device
  (see below).

``apple-dma-pci``
  A paravirtualized PCI device that provides batched DMA map/unmap
  operations between the guest and the dext.  The guest driver writes a
  command page GPA to BAR0 registers, fills request/response buffers in
  RAM, then triggers processing with a single doorbell write (one VMEXIT
  per batch).

``Dext communication``
  All host device access (config space, BAR MMIO, DMA registration,
  interrupt notification) goes through IOKit ``IOConnectCallMethod`` calls to
  the dext's ``IOUserClient``.

DMA mapping
-----------

On Linux, VFIO programs the hardware IOMMU directly via kernel ioctls.
QEMU chooses IOVAs and the kernel maps them through the IOMMU.

On macOS, the DART (Apple's IOMMU) is managed entirely by the DriverKit
extension.  QEMU cannot choose IOVAs — it can only request that a host
virtual address be mapped for DMA, and the platform assigns the resulting
IOVA.  This means the guest cannot assume any particular IOVA layout;
the ``apple-dma-pci`` companion device returns the platform-assigned IOVA
and bus address in its response entries.

Because of this architecture, a **guest kernel module** is required to
drive the ``apple-dma-pci`` device.  The module discovers the companion
device, submits map/unmap batches, and translates between guest physical
addresses and the platform-assigned DMA addresses that the passthrough
device will use.

Platform constraints
--------------------

The macOS DART imposes limits that do not exist with Linux VFIO:

- **No IOVA alignment guarantees**: the platform may return any address.
  Guest drivers that assume page-aligned or naturally-aligned DMA
  addresses must account for this.
- **Total DMA memory limit**: approximately 1.5 GB of guest memory can be
  registered for DMA at any time.
- **Mapping count limit**: approximately 64k concurrent DMA mappings.

These limits are enforced by the DART hardware and DriverKit, not by QEMU.
Exceeding them will cause map requests to fail.

``apple-dma-pci`` register interface
-------------------------------------

.. list-table::
   :header-rows: 1

   * - Offset
     - Name
     - Access
     - Description
   * - 0x00
     - VERSION
     - R
     - Protocol version
   * - 0x04
     - MANAGED_BDF
     - R
     - Guest BDF this device maps for
   * - 0x08
     - MAX_ENTRIES
     - R
     - Maximum entries per batch
   * - 0x0C
     - STATUS
     - R
     - Result of last doorbell
   * - 0x10
     - CMD_GPA_LO
     - W
     - Command page GPA [31:0]
   * - 0x14
     - CMD_GPA_HI
     - W
     - Command page GPA [63:32]
   * - 0x18
     - DOORBELL
     - W
     - Any write triggers batch processing

Interrupts
----------

The dext tracks pending hardware interrupts (MSI/MSI-X) in a bitfield
(one bit per vector, up to 256 vectors).  When a hardware interrupt fires,
the dext sets the corresponding bit and completes an asynchronous
notification to QEMU via ``IOConnectCallAsyncMethod``.  The notification
is dispatched through a GCD queue, which wakes the QEMU main loop via an
``EventNotifier`` pipe.  QEMU then atomically reads and clears the pending
bits and delivers each flagged vector to the guest.

Limitations
===========

- **Guest kernel module required**: the ``apple-dma-pci`` protocol is not
  handled by standard guest drivers.
- **No migration support**: the Apple container does not support dirty page
  tracking.
- **Interrupt delivery**: interrupts are delivered asynchronously via IOKit
  rather than directly by the kernel, adding some overhead compared to
  Linux VFIO.
- **No hot-plug**: devices must be configured at VM startup.
- **DMA constraints**: see `Platform constraints`_ above.
- **Darwin only**: the base ``vfio-pci`` device type is not user-creatable
  on Darwin; use ``vfio-apple-pci`` instead.
