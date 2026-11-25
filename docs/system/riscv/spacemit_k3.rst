Spacemit K3 (``spacemit-k3``)
======================================

The Spacemit K3 is a RISC-V SoC from Spacemit, containing 8 RVA23 compliant
X100 cores, 8 A100 cores with large vector len (1024), a real-time cpu (RCPU)
and RISC-V AIA.

The ``spacemit-k3`` QEMU machine can be used for firmware and OS development.
This machine supports the same boot paths that real hardware does and can
serve as a tool to test firmware images without access to hardware. It is
possible to boot U-Boot SPL, OpenSBI and U-Boot/EDK2. While it is possible
to boot Linux (UART works with a defconfig kernel + busybox initrd), many
of the peripherals that Linux exercises aren't modelled yet.

spacemit-k3 QEMU model features
-------------------------------

* 8 Spacemit X100 Cores
* RISC-V compliant Advanced Interrupt Architecture
* 16550A compatible UART
* Syscon/Clock Controllers
* 1 QSPI Controller with NOR Flash
* 2 SD and 1 EMMC Controllers
* 1 UFS Controller

Known limitations
-----------------

The 8 A100 cores and the Realtime CPU on the K3 SoC are not modelled.
I2C, Networking and PCIe are not modelled.

Bootrom
-------

The spacemit-k3 machine optionally accepts a bootrom binary dumped
from hardware via ``-bios``. If this file is supplied, the machine starts
executing this, just as real hardware would. The bootrom can be dumped
following `this <https://gist.github.com/asrinivasanTT/4498e3b37015cd5c6e8ca6641c2a3340>`_
guide.

In case the bootrom is not supplied, a fallback loading method is implemented.
Look for more details at the end of the next section.

Boot Modes
----------

The spacemit-k3 machine accepts a boot-mode machine property ``-M spacemit-k3,boot-mode=nor``
that determines which boot media the bootrom loads the next boot stage from.
This argument can be set to one of the following options:

1. ``boot-mode=emmc`` - SD -> EMMC -> XMODEM
2. ``boot-mode=nor`` - SD -> NOR -> XMODEM
3. ``boot-mode=nand`` - SD -> NAND -> XMODEM
4. ``boot-mode=ufs`` - SD -> UFS -> XMODEM
5. ``boot-mode=usb`` - USB FASTBOOT
6. ``boot-mode=uart`` - UART XMODEM

All boot modes try SD first. ``boot-mode=nor`` is the default option. At the
moment, the spacemit-k3 machine doesn't support NAND flash and USB, so those
boot methods aren't supported.

If a bootrom isn't supplied via ``-bios``, qemu loads the next stage from boot
media in the same manner as the bootrom would, trying to load from one boot
medium and falling back to the next if it fails to. Only the ``emmc``, ``nor``
and ``ufs`` boot methods are supported in this case. The xmodem fallback for
these methods isn't implemented either.


Building Firmware Binaries from Vendor BSP
------------------------------------------

1. OpenSBI: A bug in vendor OpenSBI causes writes to an out of bound address
and results in store faults. We need to patch 3 lines to use it

.. code-block:: bash

	$ git clone -b k3-br-v1.0.y https://github.com/spacemit-com/opensbi/
	$ cd opensbi
	$ # Patch out of bound writes
	$ sed -i 's|^\([[:space:]]*\)\(cci_enable_snoop_dvm_reqs([456]);\)|\1// \2|' platform/generic/spacemit/spacemit_k3.c
	$ CROSS_COMPILE=riscv64-unknown-linux-gnu- PLATFORM_RISCV_XLEN=64 make O=build PLATFORM=generic PLATFORM_DEFCONFIG=k3_defconfig BUILD_INFO=y -j$(nproc)
	$ # Enable logging and rebuild
	$ KCONFIG_CONFIG=build/platform/generic/kconfig/.config OPENSBI_PLATFORM_SRC_DIR=./platform/generic OPENSBI_SRC_DIR=./ OPENSBI_PLATFORM=generic ./scripts/Kconfiglib/setconfig.py ENABLE_LOGGING=y
	$ CROSS_COMPILE=riscv64-unknown-linux-gnu- PLATFORM_RISCV_XLEN=64 make O=build PLATFORM=generic BUILD_INFO=y -j$(nproc)

2. U-Boot

.. code-block:: bash

   $ git clone -b k3-br-v1.0.y https://github.com/spacemit-com/uboot-2022.10/
   $ cd uboot-2022.10
   $ make k3_defconfig
   $ CROSS_COMPILE=riscv64-unknown-linux-gnu- make -j$(nproc)

This produces 4 files ``bootinfo_block.bin``, ``bootinfo_spinor.bin``,
``FSBL.bin`` and ``u-boot.itb``.

3. EDK2: EDK2 can be used instead of U-Boot. You'll still have to use the FSBL
and bootinfo from U-Boot.

.. code-block:: bash

	$ git clone -b k3 https://github.com/spacemit-com/edk2.git
	$ git clone -b k3 https://github.com/spacemit-com/edk2-platforms.git
	$ git -C edk2 submodule update --init
	$ git -C edk2-platforms submodule update --init
	$ export WORKSPACE=$PWD
	$ export PACKAGES_PATH=$WORKSPACE/edk2:$WORKSPACE/edk2-platforms
	$ export GCC5_RISCV64_PREFIX=riscv64-unknown-linux-gnu-
	$ source edk2/edksetup.sh
	$ make -C edk2/BaseTools
	$ build -a RISCV64 -t GCC5 -b RELEASE -n $(nproc) -p Platform/Spacemit/K3/MUSE-Pico/MUSE-Pico.dsc

This will produce a ``fitimage/MUSE-Pico/MUSE-Pico.itb``. Use this instead of
``u-boot.itb``.

Building Firmware Binaries from Upstream Sources
------------------------------------------------

While upstream OpenSBI is a drop-in replacement for vendor OpenSBI, support in
upstream U-Boot is limited at the moment. The U-Boot build can only be loaded
via YMODEM via U-Boot SPL. Build instructions for U-Boot will be added once
support is more stable.

1. OpenSBI

.. code-block:: bash

	$ git clone https://github.com/riscv-software-src/opensbi/
	$ cd opensbi
	$ CROSS_COMPILE=riscv64-unknown-linux-gnu- PLATFORM_RISCV_XLEN=64 make O=build PLATFORM=generic PLATFORM_RISCV_ISA=rv64gc FW_TEXT_START=0x100000000 BUILD_INFO=y -j$(nproc)
	$ pushd build/platform/generic/firmware
	$ wget https://github.com/spacemit-com/opensbi/raw/refs/heads/k3-br-v1.0.y/platform/generic/spacemit/fw_dynamic.its
	$ mkimage -f fw_dynamic.its fw_dynamic.itb

This should produce a ``build/platform/generic/firmware/fw_dynamic.itb``. This
can replace the ``fw_dynamic.itb`` from vendor OpenSBI.

Booting with firmware on NOR Flash
----------------------------------

Creating an image

.. code-block:: bash

	$ truncate -s 8M nor.img
	$ dd if=bootinfo_spinor.bin of=nor.img bs=1 seek=0 conv=notrunc status=none
	$ dd if=FSBL.bin of=nor.img bs=1 seek=$((0x20000)) conv=notrunc status=none
	$ # esos at 0xb0000 is skipped as it is not needed
	$ dd if=fw_dynamic.itb of=nor.img bs=1 seek=$((0x1b0000)) conv=notrunc status=none
	$ dd if=u-boot.itb of=nor.img bs=1 seek=$((0x210000)) conv=notrunc status=none


To boot the image

.. code-block:: bash

	$ ./build/qemu-system-riscv64 -M spacemit-k3 \
		-nographic -drive if=mtd,format=raw,file=nor.img

Booting with firmware on SD/EMMC/UFS
------------------------------------

Creating an image

.. code-block:: bash

	$ truncate -s 32M sd.img
	$ echo 'label: gpt' | sfdisk sd.img
	$ dd if=bootinfo_block.bin of=sd.img bs=1 seek=$((0x100000)) conv=notrunc status=none
	$ dd if=FSBL.bin of=sd.img bs=1 seek=$((0x180000)) conv=notrunc status=none
	$ # esos at 0x400000 is skipped as it is not needed
	$ dd if=fw_dynamic.itb of=sd.img bs=1 seek=$((0x700000)) conv=notrunc status=none
	$ dd if=u-boot.itb of=sd.img bs=1 seek=$((0x800000)) conv=notrunc status=none


To boot the image with SD,

.. code-block:: bash

	$ ./build/qemu-system-riscv64 -M spacemit-k3 \
		-nographic -drive if=sd,format=raw,file=sd.img,index=0

To boot with EMMC, use ``-M spacemit-k3,boot-mode=emmc`` to set the boot mode
and attach the image to the EMMC controller via ``if=sd,file=sd.img,index=2``.

To boot with UFS, use ``-M spacemit-k3,boot-mode=ufs`` to set the boot mode and
attach the image to the UFS controller via ``if=scsi,file=sd.img``.

Rootfs on UFS
-------------

Most Spacemit K3 boards boot with the boot firmware on NOR flash and rootfs on
UFS.

The UFS image for this must be built with a block size of 4096. Most pre-built
distro images off the internet are built with a block size of 512 and aren't
compatible.

To build an UFS image with a rootfs,

.. code-block:: bash

	$ truncate -s 8G ufs.img
	$ # Attach this image to a loop device
	$ sudo losetup --sector-size 4096 -fP --show ufs.img
	$ # Loop device /dev/loopN
	$ sudo sfdisk /dev/loopN <<EOF
	label: gpt
	start=2048, size=131072, type=uefi,  name=ESP
	size=, type=linux, name=rootfs
	EOF
	$ # Create filesystems
	$ sudo mkfs.vfat -F 32 -S 4096 /dev/loopNp1
	$ sudo mkfs.ext4 -b 4096 /dev/loopNp2
	$ # Mount partitions
	$ sudo mkdir -p /mnt/k3/ /mnt/k3/boot/efi
	$ sudo mount /dev/loopNp2 /mnt/k3/ && sudo mount /dev/loopNp1 /mnt/k3/boot/efi/
	$ # Copy over rootfs/ESP to /mnt/k3/

To boot with this UFS image,

.. code-block:: bash

	$ ./build/qemu-system-riscv64 -M spacemit-k3 \
		-nographic -drive if=mtd,format=raw,file=nor.img \
		-drive if=scsi,format=raw,file=ufs.img


If you want to use the UFS image to store both the boot firmware and the
rootfs, write the boot firmware binaries to the SD/EMMC/UFS offsets, then
follow the steps in this section, substituting the start LBA of the ESP to a
higher value like 4096 so that the first 16MB is free for the firmware
partitions.

Booting from XMODEM
-------------------

The Spacemit K3 bootrom implements UART boot via XMODEM as a fallback in case
none of the boot media has a valid U-Boot SPL on it. This can be used in QEMU
if you supply the bootrom via ``-bios``. You can boot the ``FSBL.bin`` from
vendor U-Boot, or any M-mode payload wrapped in the header that vendor U-Boot
wraps U-Boot SPL in using this method.

To boot the ``FSBL.bin`` from the vendor U-Boot via XMODEM,

.. code-block:: bash

	$ ./build/qemu-system-riscv64 -M spacemit-k3,boot-mode=uart \
		-bios k3.bin -nographic -serial pty
	# QEMU prints a /dev/pts/N

In a separate terminal,

.. code-block:: bash

	$ picocom -b 115200 --send-cmd sx /dev/pts/N
	# Ctrl-A + Ctrl-S
	# Select FSBL.bin to send
