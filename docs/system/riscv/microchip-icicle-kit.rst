Microchip PolarFire SoC Icicle Kit (``microchip-icicle-kit``)
=============================================================

Microchip PolarFire SoC Icicle Kit integrates a PolarFire SoC, with one
SiFive's E51 plus four U54 cores and many on-chip peripherals and an FPGA.

For more details about Microchip PolarFire SoC, please see:
https://www.microchip.com/en-us/products/fpgas-and-plds/system-on-chip-fpgas/polarfire-soc-fpgas

The Icicle Kit board information can be found here:
https://www.microchip.com/en-us/development-tool/mpfs-icicle-kit-es

Supported devices
-----------------

The ``microchip-icicle-kit`` machine supports the following devices:

* 1 E51 core
* 4 U54 cores
* Core Level Interruptor (CLINT)
* Platform-Level Interrupt Controller (PLIC)
* Loosely Integrated Memory (L2-LIM)
* L2 cache controller (L2CC)
* DDR memory controller
* System controller and system services mailbox
* 5 MMUARTs
* 1 DMA controller
* 2 GEM Ethernet controllers
* 1 SDHC storage controller
* 1 Real-Time Clock

The machine has a fixed 2 GiB of RAM. Other memory sizes are rejected.
The machine always exposes all five harts. Other CPU counts are rejected.

Boot options
------------

The ``microchip-icicle-kit`` machine provides some options to run a firmware
(BIOS) or a kernel image.  QEMU follows below truth table to select the
firmware:

============= =========== ======================================
-bios          -kernel    firmware
============= =========== ======================================
none                    N this is an error
none                    Y the kernel image
NULL, default           N hss.bin
NULL, default           Y opensbi-riscv64-generic-fw_dynamic.bin
other          don't care the BIOS image
============= =========== ======================================

Direct Kernel Boot
------------------

Use the ``-kernel`` option to directly run a kernel image.  When a direct
kernel boot is requested, a device tree blob may be specified via the ``-dtb``
option.  Unlike other QEMU machines, this machine does not generate a device
tree for the kernel.  It shall be provided by the user.  The user provided DTB
should meet the following requirements:

* The ``/cpus`` node should contain at least one subnode for E51 and the number
  of subnodes should match QEMU's ``-smp`` option.

* The ``/memory`` reg size should match QEMU’s selected RAM size via the ``-m``
  option.

* It should contain a node for the CLINT device with a compatible string
  "riscv,clint0" or "sifive,clint0".  Its ``interrupts-extended`` property
  must describe the M-mode software interrupt (3) and timer interrupt (7) for
  every hart, including the E51 hart 0.

For example, the five-hart Icicle Kit CLINT node can be described as:

.. code-block:: none

    clint: clint@2000000 {
        compatible = "sifive,fu540-c000-clint", "sifive,clint0";
        reg = <0x0 0x2000000 0x0 0xc000>;
        interrupts-extended = <&cpu0_intc 3>, <&cpu0_intc 7>,
                              <&cpu1_intc 3>, <&cpu1_intc 7>,
                              <&cpu2_intc 3>, <&cpu2_intc 7>,
                              <&cpu3_intc 3>, <&cpu3_intc 7>,
                              <&cpu4_intc 3>, <&cpu4_intc 7>;
    };

Do not replace the hart 0 interrupt numbers with ``0xffffffff`` when the same
DTB is used to initialize the generic OpenSBI platform.  OpenSBI derives the
first hart and the number of CLINT register slots from these interrupt tuples;
masking hart 0 would shift the per-hart MSIP and MTIMECMP register mappings.

When ``-bios`` is not specified or set to ``default``, the OpenSBI
``fw_dynamic`` BIOS image for the ``generic`` platform is used to boot an
S-mode payload like U-Boot or OS kernel directly.

For example, the following commands show building a U-Boot image from U-Boot
mainline v2021.07 for the Microchip Icicle Kit board:

.. code-block:: bash

  $ export CROSS_COMPILE=riscv64-linux-
  $ make microchip_mpfs_icicle_defconfig

Then we can boot the machine by:

.. code-block:: bash

  $ qemu-system-riscv64 -M microchip-icicle-kit -smp 5 -m 2G \
      -sd path/to/sdcard.img \
      -nic user,model=cadence_gem \
      -nic tap,ifname=tap,model=cadence_gem,script=no \
      -display none -serial stdio \
      -kernel path/to/u-boot/build/dir/u-boot.bin \
      -dtb path/to/u-boot/build/dir/u-boot.dtb

CAVEATS:

* Check the "stdout-path" property in the /chosen node in the DTB to determine
  which serial port is used for the serial console, e.g.: if the console is set
  to the second serial port, change to use "-serial null -serial stdio".
* The default U-Boot configuration uses CONFIG_OF_SEPARATE hence the ELF image
  ``u-boot`` cannot be passed to "-kernel" as it does not contain the DTB hence
  ``u-boot.bin`` has to be used which does contain one. To use the ELF image,
  we need to change to CONFIG_OF_EMBED or CONFIG_OF_PRIOR_STAGE.

Running HSS
-----------

The ``microchip-icicle-kit`` machine can boot the Hart Software Services
(HSS_), which then loads an HSS payload containing U-Boot from an SD card.
The following flow was tested with HSS v2024.06 and Buildroot 2026.05.

Configure HSS for the ``mpfs-icicle-kit-es`` board using its default
configuration. QEMU provides the software-visible registers and deterministic
status consumed by the HSS v2024.06 DDR initialization and training flow;
it does not model the electrical properties of DDR training. HSS requires
the RISC-V bare-metal toolchain supplied by Microchip SoftConsole to be
available in ``PATH``.  Build the tested HSS version with:

.. code-block:: bash

  $ git clone https://github.com/polarfire-soc/hart-software-services.git
  $ cd hart-software-services
  $ git checkout v2024.06
  $ make BOARD=mpfs-icicle-kit-es defconfig
  $ make -j$(nproc) BOARD=mpfs-icicle-kit-es

The HSS build creates both the raw wrapper and an eNVM programming image.  QEMU
needs the complete eNVM image, including the 256-byte boot header added by the
Microchip boot mode programmer. Convert the generated Intel HEX file to a raw
binary image, for example:

.. code-block:: bash

  $ riscv64-unknown-elf-objcopy -I ihex -O binary \
      build/hss-envm-wrapper.mpfs-icicle-kit-es.hex build/hss.bin

Do not pass ``build/hss-envm-wrapper.bin`` directly to QEMU. That file starts
at eNVM offset 0x100 and does not contain the boot header with the image size
and per-hart reset vectors.

Build the SD card image with the tested Buildroot version:

.. code-block:: bash

  $ git clone https://gitlab.com/buildroot.org/buildroot.git
  $ cd buildroot
  $ git checkout 2026.05
  $ make microchip_mpfs_icicle_defconfig
  $ make

This produces ``output/images/sdcard.img`` with three GPT partitions:

* An HSS ``payload.bin`` containing U-Boot.
* A FAT partition containing ``boot.scr`` and the kernel FIT image.
* An ext4 Linux root filesystem.

The QEMU SD card model requires a power-of-two image size. Make a sparse
4 GiB working copy and relocate its backup GPT to the new end of the image:

.. code-block:: bash

  $ cp --reflink=auto output/images/sdcard.img sdcard.img
  $ truncate -s 4G sdcard.img
  $ sgdisk -e sdcard.img
  $ sgdisk -v sdcard.img

The Icicle Kit firmware device tree in the FIT image describes 2 GiB of RAM,
matching the machine's fixed RAM size. The command below keeps ``-m 2G``
explicit.  Attach the image as an SD card and route both board serial ports:

.. code-block:: bash

  $ qemu-system-riscv64 \
      -M microchip-icicle-kit -smp 5 -m 2G \
      -bios path/to/hss/build/hss.bin \
      -drive if=sd,file=path/to/sdcard.img,format=raw \
      -display none \
      -serial file:hss.log \
      -serial stdio \
      -no-reboot

HSS writes to MMUART0, which the command records in ``hss.log``. U-Boot and
Linux use MMUART1, which remains connected to the terminal. A successful
boot proceeds through HSS payload loading, U-Boot, the Linux kernel, and the
login prompt from the root filesystem on the third partition.

Known limitations
-----------------

* The tested HSS v2024.06 flow contains 2 separate multi-hart startup races
  issues which is still not fixed as of the latest v2026.04 release. A boot
  may therefore stall at the very beginning or after successful DDR training
  during the OpenSBI/U-Boot handoff.
* The SD card model requires the raw image size to be a power of two. Keep
  the backup GPT header at the end when resizing the image.
* The machine does not generate an Icicle Kit device tree. Firmware boot must
  provide one in its payload or FIT image; direct kernel boot must use
  ``-dtb`` as described above.

.. _HSS: https://github.com/polarfire-soc/hart-software-services
