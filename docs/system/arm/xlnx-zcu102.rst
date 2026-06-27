Xilinx ZynqMP ZCU102 (``xlnx-zcu102``)
======================================

The ``xlnx-zcu102`` board models the Xilinx ZynqMP ZCU102 board.
This board has 4 Cortex-A53 CPUs and 2 Cortex-R5F CPUs.

Supported devices
-----------------

The machine is based on QEMU's Xilinx ZynqMP SoC model and includes
the following Processing System devices:

 * Cortex-A53 APU and Cortex-R5F RPU CPU cores
 * Generic Interrupt Controller
 * DDR memory
 * On-chip memory
 * Cadence UARTs
 * Cadence GEM Ethernet controllers
 * SDHCI controllers
 * SPI controllers
 * GQSPI controller
 * RTC
 * CAN controllers
 * USB controllers
 * SATA controller
 * DMA controllers
 * BBRAM and eFUSE devices

Machine-specific options
------------------------

The following machine-specific options are supported:

secure
  Set ``on``/``off`` to enable/disable emulating a guest CPU which implements the
  Arm Security Extensions (TrustZone). The default is ``off``.

virtualization
  Set ``on``/``off`` to enable/disable emulating a guest CPU which implements the
  Arm Virtualization Extensions. The default is ``off``.

Boot options
------------

The ``xlnx-zcu102`` machine can start a Linux kernel directly with
``-kernel``. It does not provide a ZynqMP BootROM flow for loading a
Xilinx boot image from SD or QSPI flash, so images such as ``boot.bin``
or ``qspi.bin`` cannot be passed alone and expected to boot like they
would on real hardware.

QEMU does not generate a built-in device tree for this machine. Pass a DTB
with ``-dtb``. For direct Linux boot with a real-board ZCU102 DTB, QEMU
applies compatibility adjustments for the boot-critical UART, SDHCI and
GQSPI nodes so that Linux can use the devices modeled by QEMU without
requiring user-side DTB edits.

This is not the same as emulating the complete ZynqMP firmware stack. The
DTB can still contain non-boot devices that depend on board peripherals,
external clock chips, power domains, pin control, or firmware services
that QEMU does not model.

Direct Linux boot with Buildroot
--------------------------------

Buildroot has a ZCU102 defconfig. Buildroot 2026.05 release is tested at the
time of writing. From the Buildroot source tree:

.. code-block:: bash

  $ make zynqmp_zcu102_defconfig
  $ make

The generated files are in ``output/images/``. The examples below use:

 * ``Image``
 * ``zynqmp-zcu102-rev1.0.dtb``
 * ``sdcard.img``
 * ``qspi.bin`` if QSPI flash probing is desired

QEMU's SD card model requires a power-of-two image size. Work on a copy
of the Buildroot SD image and resize the copy, not the original output:

.. code-block:: bash

  $ cp output/images/sdcard.img sdcard-qemu.img
  $ qemu-img resize -f raw sdcard-qemu.img 128M

The real-board DTB enables the SDHCI controller at ``mmc@ff170000``.
In QEMU this is SD index 1, so attach the SD card with
``if=sd,index=1``.

Boot Linux with the Buildroot kernel and the native Buildroot DTB:

.. code-block:: bash

  $ qemu-system-aarch64 -M xlnx-zcu102 -m 2G -nographic \
      -kernel output/images/Image \
      -dtb output/images/zynqmp-zcu102-rev1.0.dtb \
      -append "earlycon=cdns,mmio,0xff000000,115200n8 \
               console=ttyPS0,115200 root=/dev/mmcblk0p2 rw rootwait" \
      -drive file=sdcard-qemu.img,if=sd,index=1,format=raw

This should get the kernel booting all the way to the Buildroot login
prompt on ``ttyPS0``.

QSPI flash
----------

The board creates two PS SPI NOR flashes first and the ZynqMP GQSPI
flashes after them. Therefore the first GQSPI flash is
``if=mtd,index=2``. The remaining GQSPI flash backends use indices 3, 4
and 5.

The Buildroot ``qspi.bin`` image is smaller than the 64 MiB
``n25q512a11`` flash model. To attach it as the first GQSPI flash,
place it in a 64 MiB raw backing file:

.. code-block:: bash

  $ qemu-img create -f raw qspi-cs0.img 64M
  $ dd if=output/images/qspi.bin of=qspi-cs0.img bs=1M conv=notrunc

Add the following drive to the direct Linux boot command above:

.. code-block:: bash

      -drive file=qspi-cs0.img,if=mtd,index=2,format=raw

With this drive attached, Linux should probe the ZynqMP GQSPI
controller and register the SPI NOR partitions from the DTB, for
example ``qspi-fsbl-uboot``, ``qspi-linux``, ``qspi-device-tree`` and
``qspi-rootfs``.

The default Buildroot ``qspi.bin`` is a boot image, not a root
filesystem image. The default DTB's ``qspi-rootfs`` partition is also
too small for Buildroot's ``rootfs.ext2``. Booting with the root
filesystem in QSPI flash therefore requires a custom image layout and
matching DTB changes.

Known limitations
-----------------

The machine does not emulate the complete real-board firmware stack.
In particular:

 * ZynqMP BootROM boot-mode selection is not modeled.
 * Passing the Buildroot ``boot.bin`` with ``-bios`` does not boot Linux.
 * Attaching ``qspi.bin`` as an MTD drive without ``-kernel`` does not
   boot from QSPI flash.
 * ZynqMP PM firmware services are not modeled. For direct Linux boot,
   QEMU adjusts the supplied DTB only for the boot-critical UART, SDHCI
   and GQSPI nodes.
 * Other real-board DTB nodes can still defer or fail probing when they
   depend on devices that are not present in the QEMU model, such as
   external I2C clock generators, board sensors, some GPIO consumers,
   PHYs, or non-boot DMA/display paths.
