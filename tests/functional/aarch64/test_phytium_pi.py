#!/usr/bin/env python3
#
# Functional tests for the Phytium Pi machine
#
# Copyright (c) 2026 Process Mission
#
# Author:
#   Bin Meng <bin.meng@processmission.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset, LinuxKernelTest
from qemu_test import skipBigDataTest, skipIfMissingCommands


class PhytiumPiMachine(LinuxKernelTest):

    ASSET_BUILDROOT = Asset(
        ('https://github.com/processmission/qemu-machine-images/releases/'
         'download/v1.0.0/aarch64-phytium-pi-v1.0.0.tar.zst'),
        '5219d52b862e1245b12f79ba72e9a6144f5e0cf1a50061f8fda2db0aeeb92428')

    def _prepare_images(self):
        self.set_machine('phytium-pi')

        archive_path = self.uncompress(
            self.ASSET_BUILDROOT,
            target='aarch64-phytium-pi-v1.0.0.tar',
            format='zstd')
        self.archive_extract(archive_path, format='tar')

        self.vm.set_console(console_index=1)
        self.vm.add_args('-smp', '4',
                         '-m', '4G',
                         '-display', 'none',
                         '-nic', 'none',
                         '-no-reboot')

    @skipIfMissingCommands('zstd')
    @skipBigDataTest()
    def test_firmware_boot(self):
        self._prepare_images()
        sdcard = self.scratch_file('images', 'sdcard.img')

        self.vm.add_args(
            '-snapshot',
            '-drive', f'file={sdcard},format=raw,if=sd,index=0')
        self.vm.launch()

        self.wait_for_console_pattern('U-Boot 2022.01')
        self.wait_for_console_pattern('PBF relocate done')
        self.wait_for_console_pattern('Starting kernel ...')
        self.wait_for_console_pattern('Phytium login:')

    @skipIfMissingCommands('zstd')
    @skipBigDataTest()
    def test_linux_boot(self):
        self._prepare_images()
        kernel = self.scratch_file('images', 'Image.gz')
        dtb = self.scratch_file('images', 'phytiumpi_firefly.dtb')
        initrd = self.scratch_file('images', 'rootfs.cpio.gz')

        self.vm.add_args(
            '-kernel', kernel,
            '-dtb', dtb,
            '-initrd', initrd,
            '-append', 'console=ttyAMA1,115200 '
                       'earlycon=pl011,mmio32,0x2800d000 rdinit=/init')
        self.vm.launch()

        self.wait_for_console_pattern('Booting Linux on physical CPU')
        self.wait_for_console_pattern('Machine model: Phytium Pi Board')
        self.wait_for_console_pattern('Phytium login:')


if __name__ == '__main__':
    LinuxKernelTest.main()
