.. SPDX-License-Identifier: GPL-2.0-or-later

Axiado family boards (``axiado-scm3003``, ``ax3005-evb``)
================================================================

The Axiado AX3000 and AX3005 are Arm-based system-on-chip devices intended
to be used as the management and security processor of a server platform,
in the same role as a traditional Baseboard Management Controller (BMC).

Two machines are modelled:

- ``axiado-scm3003``  Axiado SCM3003 evaluation kit, based on the AX3000
- ``ax3005-evb``      Axiado AX3005 evaluation board, based on the AX3005

Neither SoC is supported by the upstream Linux kernel or by upstream
U-Boot, so there is currently no way to build a bootable image for these
machines from mainline sources. Axiado intends to publish a firmware
package suitable for QEMU; until then no ready-to-run image is publicly
available.

Firmware download
-----------------

Images can be downloaded from the Axiado Forked OpenBMC Github release repository :

    https://github.com/axiado/openbmc/releases

AX3000 (``axiado-scm3003``)
---------------------------

Supported devices
~~~~~~~~~~~~~~~~~

 * SMP (4 x Cortex-A53)
 * GICv3 interrupt controller
 * Arm generic timers
 * DRAM
 * Cadence UARTs
 * Cadence GPIO controllers
 * SD/eMMC host controller (SDHCI) with its eMMC PHY register block
 * Clock/reset controller (PLL) registers polled by the boot firmware

Running
~~~~~~~

Boot a kernel directly on the SCM3003 evaluation kit, with an eMMC/SD
image attached to the SD host controller:

.. code-block:: bash

  qemu-system-aarch64 -M axiado-scm3003 -nographic \
      -kernel Image \
      -dtb ax3000-scm3003-qemu.dtb \
      -initrd obmc-phosphor-initramfs-evk-axiado-qemu.cpio.xz \
      -serial null \
      -serial null \
      -serial null \
      -serial mon:stdio \
      -drive if=sd,file=obmc-phosphor-image-evk-axiado-qemu.wic.qcow2,format=qcow2 \
      -append "console=ttyPS3,115200 earlycon nr_cpus=1 rootwait root=PARTLABEL=rofs-a"

or boot via u-boot:

.. code-block:: bash

  qemu-system-aarch64 -M axiado-scm3003 -nographic \
      -device loader,file=u-boot.bin,addr=0x3C000000,cpu-num=0 \
      -serial null \
      -serial null \
      -serial null \
      -serial mon:stdio \
      -drive if=sd,file=obmc-phosphor-image-evk-axiado-qemu.wic.qcow2,format=qcow2

AX3005 (``ax3005-evb``)
-----------------------

Supported devices
~~~~~~~~~~~~~~~~~

 * SMP (4 x Cortex-A53)
 * GICv3 interrupt controller
 * Arm generic timers
 * DRAM
 * Cadence UARTs
 * Cadence GPIO controllers
 * SD/eMMC host controller (SDHCI) with its eMMC PHY register block

Running
~~~~~~~

Boot a kernel directly on the AX3005 evaluation board, with an eMMC/SD
image attached to the SD host controller:

.. code-block:: bash

  qemu-system-aarch64 -M ax3005-evb -nographic \
      -M ax3005-evb \
      -kernel Image \
      -dtb ax3005-evb-qemu.dtb \
      -initrd obmc-phosphor-initramfs-evk-ax3005-qemu.cpio.xz \
      -device loader,file=obmc-phosphor-image-evk-ax3005-qemu.squashfs-xz,addr=0x90000000 \
      -serial null \
      -serial null \
      -serial null \
      -serial mon:stdio \
      -drive if=sd,file=obmc-phosphor-image-evk-ax3005-qemu.wic.qcow2,format=qcow2 \
      -append "console=ttyPS3,115200 earlycon nr_cpus=1 root=/dev/ram rw phram.phram=ramrofs,0x90000000,0x6400000"

or boot with ARM TrustZone:

.. code-block:: bash

  qemu-system-aarch64 -nographic \
      -M ax3005-evb \
      -device loader,file=bl31.bin,addr=0x88300000 \
      -device loader,file=tee-raw.bin,addr=0x88400000 \
      -device loader,file=u-boot.bin,addr=0x80000000 \
      -device loader,file=fitImage,addr=0x80100000 \
      -device loader,file=obmc-phosphor-image-evk-ax3005-qemu.squashfs-xz,addr=0x80B00000 \
      -device loader,cpu-num=0,addr=0x88300000 \
      -device loader,cpu-num=1,addr=0x88300000 \
      -device loader,cpu-num=2,addr=0x88300000 \
      -device loader,cpu-num=3,addr=0x88300000 \
      -serial null \
      -serial null \
      -serial null \
      -serial mon:stdio \
      -drive if=sd,file=obmc-phosphor-image-evk-ax3005-qemu.wic.qcow2,format=qcow2
