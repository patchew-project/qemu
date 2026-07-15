ASPEED AST2700 UFS Host Controller
===================================

The AST2700 SoC includes a UFS host controller identified in the device tree
as ``aspeed,ufshc-m31-16nm``, mapped at ``0x12c08200`` (IRQ SPI 118). QEMU
models it as a sysbus device implementing the UFSHCI v2.0 register interface
required by U-Boot and the Linux ``aspeed-ufshcd`` driver.

The clock/reset wrapper at ``0x12c08000`` (``aspeed,ast2700-ufscnr``) is left
as an ``UnimplementedDevice``.

Implemented functionality
--------------------------

Host Controller Enable (HCE)
  On a ``HCE=1`` write the register is cleared to 0 and a bottom-half is
  scheduled to set it back to 1 with ``HCS`` ready bits. The read handler
  returns 0 while in phase 1, so polling sees the 0 to 1 transition without 
  a timeout. If the controller is already in the ready state (phase 2), a
  subsequent ``HCE=1`` write is a no-op; the controller stays enabled for
  Linux after U-Boot has left it running.

UIC commands
  ``DME_LINKSTARTUP``, ``DME_GET``, ``DME_PEER_GET``, ``DME_SET``, and
  ``DME_PEER_SET`` all succeed immediately. ``DME_LINKSTARTUP`` sets
  ``HCS.DP`` and raises ``IS.UIC_LINK_STARTUP`` (bit 8). ``DME_SET`` and
  ``DME_PEER_SET`` set ``HCS[10:8] = PWR_LOCAL`` and raise
  ``IS.UIC_POWER_MODE``.

NOP OUT / NOP IN
  Doorbell processing is synchronous (inline) rather than deferred, as
  U-Boot's ``NOP_OUT_TIMEOUT`` is shorter than a deferred bottom-half can
  fire in QEMU's event loop.

SCSI block I/O
  READ_10, READ_16, WRITE_10, WRITE_16, INQUIRY, READ_CAPACITY_10/16, and
  REPORT_LUNS are forwarded to the attached ``BlockBackend``. When
  ``prdtl = 0`` (no scatter-gather list), response data is placed in the
  Response UPIU data segment; when ``prdtl > 0`` it is written into the PRDT
  buffers.

Query UPIU descriptors
  Device, Geometry, Unit, Power (idn 8), and String (idn 5) descriptors are
  returned, capped to the requested length.

Well-Known LUNs
  All four W-LUNs (0xD0 UFS Device, 0xC4 RPMB, 0xB0 Boot, 0x81 Report LUNs)
  respond as present devices.

Task management (UTMRL)
  ``LOGICAL_UNIT_RESET`` commands are acknowledged immediately by writing
  ``OCS_SUCCESS`` into the UTMRD slot, clearing the doorbell, and raising
  ``IS.UTP_TASK_REQ_COMPL`` (bit 9).

Usage
-----

The UFS controller picks up the first ``IF_NONE`` block device at index 0.
Pass a UFS disk image with ``-drive if=none``:

.. code-block:: console

  qemu-system-aarch64 -M huygens-bmc \
    -drive file=image-bmc,if=mtd,format=raw \
    -drive file=ufs.img,if=none,format=raw \
    -nographic

Please check :doc:`../../system/arm/aspeed` for more details on the
``huygens-bmc`` machine.
