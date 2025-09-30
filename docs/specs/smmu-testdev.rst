smmu-testdev — Minimal SMMUv3 DMA test device
=============================================

Overview
--------
``smmu-testdev`` is a tiny, test-only DMA source intended to exercise the
SMMUv3 emulation without booting firmware or a guest OS. It lets tests
populate STE/CD/PTE with known values and trigger a DMA that flows through
the SMMU translation path. It is **not** a faithful PCIe endpoint nor a
platform device and must be considered a QEMU-internal test vehicle.

Status
------
* Location: ``hw/misc/smmu-testdev.c``
* Build guard: ``CONFIG_SMMU_TESTDEV``
* Default machines: none (tests instantiate it explicitly)
* Intended use: qtests under ``tests/qtest/smmu-testdev-qtest.c``

Running the qtest
-----------------
The smoke test ships with this device and is the recommended entry point::

    QTEST_QEMU_BINARY=qemu-system-aarch64 ./tests/qtest/smmu-testdev-qtest
     --tap -k

This programs a minimal Non-Secure SMMU context, kicks a DMA, and verifies
translation + data integrity.

Instantiation (advanced)
------------------------
The device is not wired into any board by default. For ad-hoc experiments,
tests (or developers) can create it dynamically via qtest or the QEMU
monitor. It exposes a single MMIO window that the test drives directly.

Limitations
-----------
* Non-Secure bank only in this version; Secure SMMU tests are planned once
  upstream Secure support lands.
* No PCIe discovery, MSI, ATS/PRI, or driver bring-up is modeled.
* The device is test-only; do not rely on it for machine realism.

See also
--------
* ``tests/qtest/smmu-testdev-qtest.c`` — the companion smoke test
* SMMUv3 emulation and documentation under ``hw/arm/smmu*``
