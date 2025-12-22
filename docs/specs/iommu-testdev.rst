iommu-testdev — IOMMU test device for bare-metal testing
=========================================================

Overview
--------
``iommu-testdev`` is a minimal, test-only PCI device designed to exercise
IOMMU translation (such as ARM SMMUv3) without requiring firmware or a guest
OS. Tests can populate IOMMU translation tables with known values and trigger
DMA operations that flow through the IOMMU translation path. It is **not** a
faithful PCIe endpoint and must be considered a QEMU-internal test vehicle.

Key Features
------------
* **Bare-metal IOMMU testing**: No guest kernel or firmware required
* **Configurable DMA attributes**: Supports address space configuration via
  MMIO registers
* **Deterministic verification**: Write-then-read DMA pattern with automatic
  result checking

Status
------
* Location: ``hw/misc/iommu-testdev.c``
* Header: ``include/hw/misc/iommu-testdev.h``
* Build guard: ``CONFIG_IOMMU_TESTDEV``

Device Interface
----------------
The device exposes a single PCI BAR0 with 32bit MMIO registers:

* ``ITD_REG_DMA_TRIGGERING`` (0x00): Reading triggers DMA execution
* ``ITD_REG_DMA_GVA_LO`` (0x04): GVA bits [31:0]
* ``ITD_REG_DMA_GVA_HI`` (0x08): GVA bits [63:32]
* ``ITD_REG_DMA_LEN`` (0x0C): DMA transfer length
* ``ITD_REG_DMA_RESULT`` (0x10): DMA operation result (0=success)
* ``ITD_REG_DMA_DBELL`` (0x14): Write 1 to arm DMA
* ``ITD_REG_DMA_ATTRS`` (0x18): DMA attributes which shadow MemTxAttrs format:

  - bit[0]: secure (1=Secure, 0=Non-Secure)
  - bits[2:1]: address space (0=Non-Secure, 1=Secure)
    Only these MemTxAttrs fields (``secure`` and ``space``) are consumed today;
    other bits are reserved but can be wired up easily if future tests need
    to pass extra attributes.

Translation Setup Workflow
--------------------------
``iommu-testdev`` never builds SMMU/AMD-Vi/RISC-V IOMMU structures on its own.
Architecture-specific construction lives entirely in qtest/libqos helpers.
Those helpers populate guest memory with page tables/architecture-specific
structures and program the emulated IOMMU registers directly. See the
``qsmmu_setup_and_enable_translation()`` function in
``tests/qtest/libqos/qos-smmuv3.c`` for an example of how SMMUv3 translation
is set up for this device, which will be introduced in the next commit.

DMA Operation Flow
------------------
The flow would be split into these steps, mainly for timing control and
debuggability: qtests can easily exercise and assert distinct paths
(NOT_ARMED, BAD_LEN, TX/RD failures, mismatch) instead of having all side
effects hidden behind a single step:
1. Test programs IOMMU translation tables
2. Test configures DMA address (GVA_LO/HI), length, and attributes
3. Test writes 1 to DMA_DBELL to arm the operation
4. Test reads DMA_TRIGGERING to execute DMA
5. Test polls DMA_RESULT:

   - 0x00000000: Success
   - 0xFFFFFFFE: Busy (still in progress)
   - 0xDEAD000X: Various error codes

The device performs a write-then-read sequence using a known pattern
(0x12345678) and verifies data integrity automatically.

Running the qtest
-----------------
The SMMUv3 test suite uses this device and covers multiple translation modes::

    cd build-debug
    QTEST_QEMU_BINARY=./qemu-system-aarch64 \\
        ./tests/qtest/iommu-smmuv3-test --tap -k

This test suite exercises:

* Stage 1 only translation
* Stage 2 only translation
* Nested (Stage 1 + Stage 2) translation

Instantiation
-------------
The device is not wired into any board by default. Tests instantiate it
via QEMU command line::

    -device iommu-testdev

For ARM platforms with SMMUv3::

    -M virt,iommu=smmuv3 -device iommu-testdev

The device will be placed behind the IOMMU automatically.

Limitations
-----------
* No realistic PCIe enumeration, MSI/MSI-X, or interrupt handling
* No ATS/PRI support
* No actual device functionality beyond DMA test pattern
* Test-only; not suitable for production or machine realism
* Address space support (Secure/Root/Realm) is architecture-dependent

See also
--------
* ``tests/qtest/iommu-smmuv3-test.c`` — SMMUv3 test suite
* ``tests/qtest/libqos/qos-smmuv3.{c,h}`` — SMMUv3 test library
* SMMUv3 emulation: ``hw/arm/smmu*``
