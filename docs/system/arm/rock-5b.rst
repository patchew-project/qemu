.. SPDX-License-Identifier: GPL-2.0-or-later

Radxa ROCK 5B (``rock-5b``)
===========================

The ``rock-5b`` machine models the Radxa ROCK 5B single-board computer,
which is built around the Rockchip RK3588 SoC.

Supported devices
-----------------

The ``rock-5b`` machine supports the following devices:

* 4 Cortex-A55 and 4 Cortex-A76 CPUs
* GICv3 and the architectural timers
* On-chip SRAM
* HPTIMER boot control register (enable bit)
* UART1 through UART9
* Rockchip SFC with a 16 MiB SPI NOR flash
* Clock and reset unit (CRU) registers
* SCMI Base and Clock protocols over SMC (CPU and DSU clocks)
* OTP controller

The machine always uses all eight CPUs and supports 128 MiB to 16 GiB
of RAM, with 2 GiB as the default.

Boot options
------------

The ``rock-5b`` machine boots U-Boot SPL when ``-bios`` is given and
Linux directly when ``-kernel`` is given; one of the two is required.
Both methods start the primary CPU in non-secure EL2 and provide PSCI
through SMC calls. DRAM is assumed to be initialized;
BootROM, DDR training firmware and BL31 are not modeled.

U-Boot SPL is supplied as a raw binary with ``-bios``. QEMU loads it
at ``0x00000000``; SPL loads U-Boot proper from SPI NOR. The device tree
is taken from the Linux FIT image loaded by U-Boot, so ``-dtb`` cannot be
used with this method.

Direct Linux boot supplies an AArch64 Linux ``Image`` with ``-kernel``
and a device tree with ``-dtb``; ``-initrd`` and ``-append`` are optional.
QEMU does not generate a device tree for this machine.

Running
-------

The first ``-serial`` option connects to UART2, the debug console on
the board's header; further ``-serial`` options connect to UART1 and
UART3 through UART9 in that order.

U-Boot SPL boot
~~~~~~~~~~~~~~~

With ``u-boot-spl.bin`` and ``spi-nor.img``:

.. code-block:: bash

   $ qemu-system-aarch64 \
       -machine rock-5b -m 2G -smp 8 \
       -display none -monitor none -serial stdio \
       -bios u-boot-spl.bin \
       -drive if=mtd,index=0,file=spi-nor.img,format=raw

The SPI NOR image must be exactly 16 MiB. Add ``snapshot=on`` to the
drive option to discard guest writes.

Direct Linux boot
~~~~~~~~~~~~~~~~~

With ``Image``, ``rootfs.cpio.gz`` and ``rk3588-rock-5b.dtb``:

.. code-block:: bash

   $ qemu-system-aarch64 \
       -machine rock-5b -m 2G -smp 8 \
       -display none -monitor none -serial stdio \
       -kernel Image -initrd rootfs.cpio.gz -dtb rk3588-rock-5b.dtb \
       -append "console=ttyS2,1500000n8 rdinit=/init"

Limitations
-----------

The machine is TCG-only; KVM is not supported. Only the devices listed
above are modeled, and the remaining RK3588 peripherals are unimplemented.
Migration and VM snapshots are unsupported.
