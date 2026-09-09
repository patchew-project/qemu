#!/usr/bin/env python3
#
# Functional test that boots an OpenBMC image on the Axiado AX3005 EVB
#
# Author: Kuan-Jui Chiu <kchiu@axiado.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset, wait_for_console_pattern


class Ax3005EvbMachine(LinuxKernelTest):

    ASSET_SDK_IMAGES = Asset(
        ('https://github.com/axiado/openbmc/releases/download/v0.1.0/'
         'evk-ax3005-qemu.tar.gz'),
        'e07e89151981fb4730223dfe399a7111a0367e3c4122e21d65ef4aa8a2ba04c7')

    ATF_BASE = 0x88300000
    OPTEE_BASE = 0x88400000
    UBOOT_BASE = 0x80000000
    FIT_BASE = 0x80100000
    RAMROFS_BASE = 0x80B00000
    RAMROFS_SIZE = 0x6400000

    def setUp(self):
        super().setUp()

        SDK_IMAGE_DIR = 'evk-ax3005-qemu'

        self.archive_extract(self.ASSET_SDK_IMAGES)
        self.atf_path = self.scratch_file(SDK_IMAGE_DIR,
                                          'trusted-firmware-a/bl31.bin')
        self.optee_path = self.scratch_file(SDK_IMAGE_DIR, 'optee/tee-raw.bin')
        self.uboot_path = self.scratch_file(SDK_IMAGE_DIR, 'u-boot.bin')
        self.fit_path = self.scratch_file(SDK_IMAGE_DIR, 'fitImage')
        self.kernel_path = self.scratch_file(SDK_IMAGE_DIR, 'Image')
        self.dtb_path = self.scratch_file(SDK_IMAGE_DIR, 'ax3005-evb-qemu.dtb')
        self.initrd_path = self.scratch_file(
            SDK_IMAGE_DIR, 'obmc-phosphor-initramfs-evk-ax3005-qemu.cpio.xz')
        self.rofs_path = self.scratch_file(
            SDK_IMAGE_DIR, 'obmc-phosphor-image-evk-ax3005-qemu.squashfs-xz')
        self.disk_path = self.scratch_file(
            SDK_IMAGE_DIR, 'obmc-phosphor-image-evk-ax3005-qemu.wic.qcow2')

    def test_aarch64_ax3005_evb_boot(self):
        self.require_accelerator('tcg')
        self.set_machine('ax3005-evb')

        self.RAMROFS_BASE = 0x90000000

        # The SoC has nine Cadence UARTs; the console is the fourth one.
        self.vm.set_console(console_index=3)
        self.vm.add_args(
            '-kernel', self.kernel_path,
            '-dtb', self.dtb_path,
            '-initrd', self.initrd_path,
            '-device', 'loader,file=%s,addr=0x%x' % (self.rofs_path,
                                                     self.RAMROFS_BASE),
            '-drive', 'file=%s,if=sd,format=qcow2,snapshot=on' % self.disk_path,
            '-append', 'console=ttyPS3,115200 earlycon nr_cpus=1 '
                       'root=/dev/ram rw '
                       'phram.phram=ramrofs,0x%x,0x%x' % (self.RAMROFS_BASE,
                                                          self.RAMROFS_SIZE))

        self.vm.launch()

        self.wait_for_console_pattern('Linux version')
        self.wait_for_console_pattern('Machine model: Axiado AX3005 EVB QEMU')

        # The reservation comes from the board's modify_dtb() hook, not from
        # the supplied DTB, which still carries the old address.
        self.wait_for_console_pattern(
            'OF: reserved mem: 0x0000000090000000..0x00000000963fffff')

        # phram claims the reserved window and exposes it as an MTD device.
        wait_for_console_pattern(
            self, 'phram: ramrofs device: 0x6400000 at 0x90000000',
            failure_message='phram: Failed to register new device')

        self.wait_for_console_pattern('Run /init as init process')

        # The initramfs prints this after probing the SD card for the rwfs.
        self.wait_for_console_pattern('blkdev: /dev/mmcblk0 sector_size: 512')

        # Reaching systemd proves the initramfs mounted the squashfs out of
        # the phram window and switch_root'ed into it.
        self.wait_for_console_pattern('Hostname set to <evk-ax3005-qemu>')

    def test_aarch64_ax3005_evb_tzboot(self):
        self.require_accelerator('tcg')
        self.set_machine('ax3005-evb')

        # The SoC has nine Cadence UARTs; the console is the fourth one.
        self.vm.set_console(console_index=3)
        self.vm.add_args(
            '-device', 'loader,file=%s,addr=0x%x' % (self.atf_path,
                                                     self.ATF_BASE),
            '-device', 'loader,file=%s,addr=0x%x' % (self.optee_path,
                                                     self.OPTEE_BASE),
            '-device', 'loader,file=%s,addr=0x%x' % (self.uboot_path,
                                                     self.UBOOT_BASE),
            '-device', 'loader,file=%s,addr=0x%x' % (self.fit_path,
                                                     self.FIT_BASE),
            '-device', 'loader,file=%s,addr=0x%x' % (self.rofs_path,
                                                     self.RAMROFS_BASE),
            '-device', 'loader,cpu-num=0,addr=0x%x' % self.ATF_BASE,
            '-device', 'loader,cpu-num=1,addr=0x%x' % self.ATF_BASE,
            '-device', 'loader,cpu-num=2,addr=0x%x' % self.ATF_BASE,
            '-device', 'loader,cpu-num=3,addr=0x%x' % self.ATF_BASE,
            '-drive', 'file=%s,if=sd,format=qcow2,snapshot=on' % self.disk_path)

        self.vm.launch()

        self.wait_for_console_pattern('BL31: Initializing BL32')
        self.wait_for_console_pattern('OP-TEE version:')
        self.wait_for_console_pattern('Primary CPU switching to normal world')

        self.wait_for_console_pattern('U-Boot 2026.04')
        self.wait_for_console_pattern('Starting kernel')

        self.wait_for_console_pattern('Linux version')
        self.wait_for_console_pattern('Machine model: Axiado AX3005 EVB QEMU')

        # phram claims the reserved window and exposes it as an MTD device.
        wait_for_console_pattern(
            self, 'phram: ramrofs device: 0x6400000 at 0x80b00000',
            failure_message='phram: Failed to register new device')

        self.wait_for_console_pattern('Run /init as init process')

        # The initramfs prints this after probing the SD card for the rwfs.
        self.wait_for_console_pattern('blkdev: /dev/mmcblk0 sector_size: 512')

        # Reaching systemd proves the initramfs mounted the squashfs out of
        # the phram window and switch_root'ed into it.
        self.wait_for_console_pattern('Hostname set to <evk-ax3005-qemu>')

if __name__ == '__main__':
    LinuxKernelTest.main()
