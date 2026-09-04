#!/usr/bin/env python3
#
# Functional tests for the NXP MCIMX6UL-EVK machine
#
# Copyright (c) 2026 Process Mission
#
# Author:
#   Bin Meng <bin.meng@processmission.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset, LinuxKernelTest, skipIfMissingCommands


class MCIMX6ULEVKMachine(LinuxKernelTest):

    ASSET_BUILDROOT = Asset(
        ('https://github.com/processmission/qemu-machine-images/releases/download/'
         'v1.0.0/arm-mcimx6ul-evk-v1.0.0.tar.zst'),
        'a08f2b5dae139cf73cfa601c9df9316356a07a5dc59c74d99f30ebee61c405fb')

    def _prepare_images(self):
        self.set_machine('mcimx6ul-evk')

        archive_path = self.uncompress(
            self.ASSET_BUILDROOT,
            target='arm-mcimx6ul-evk-v1.0.0.tar',
            format='zstd')
        self.archive_extract(archive_path, format='tar')

        sdcard_path = self.scratch_file('images', 'sdcard.img')
        self.vm.set_console()
        self.vm.add_args(
            '-m', '512M',
            '-snapshot',
            '-drive', f'file={sdcard_path},format=raw,if=sd,index=1',
            '-no-reboot')

    @skipIfMissingCommands('zstd')
    def test_uboot_boot(self):
        self._prepare_images()

        uboot_path = self.scratch_file('images', 'u-boot.bin')
        self.vm.add_args(
            '-device', f'loader,file={uboot_path},addr=0x87800000,cpu-num=0')
        self.vm.launch()

        self.wait_for_console_pattern('U-Boot 2024.07')
        self.wait_for_console_pattern('Starting kernel ...')
        self.wait_for_console_pattern('buildroot login:')

    @skipIfMissingCommands('zstd')
    def test_linux_boot(self):
        self._prepare_images()

        self.vm.add_args(
            '-kernel', self.scratch_file('images', 'zImage'),
            '-dtb', self.scratch_file('images',
                                      'imx6ul-14x14-evk.dtb'),
            '-append',
            'console=ttymxc0,115200 root=/dev/mmcblk1p2 rootwait rw')
        self.vm.launch()

        self.wait_for_console_pattern('Linux version')
        self.wait_for_console_pattern('buildroot login:')


if __name__ == '__main__':
    LinuxKernelTest.main()
