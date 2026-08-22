#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel on a Milk-V Duo machine
#
# Copyright (c) 2026 Kuan-Wei Chiu <visitorckw@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset
from qemu_test import wait_for_console_pattern

class MilkvDuoMachine(LinuxKernelTest):

    timeout = 120

    ASSET_KERNEL = Asset(
        'https://github.com/visitorckw/milkv-duo-qemu-assets/releases/download/v1.0/Image',
        '3f157ecfcb86d28bfcff9fd3c1a43f60a8513b1919739fdc9a4d946aba35b2af'
    )
    ASSET_DTB = Asset(
        'https://github.com/visitorckw/milkv-duo-qemu-assets/releases/download/v1.0/cv1800b-milkv-duo.dtb',
        '2a5d165f64c4b2d55bba282b2974941ab223f6f5220c2e7721e35ab81311c098'
    )
    ASSET_ROOTFS = Asset(
        'https://github.com/visitorckw/milkv-duo-qemu-assets/releases/download/v1.0/rootfs.ext4',
        '9e7de9222ab4a79d1291866453aa47cd67a543216df6159bf9b42d055240a676'
    )

    def test_riscv64_milkv_duo(self):
        self.set_machine('milkv-duo')

        kernel_path = self.ASSET_KERNEL.fetch()
        dtb_path = self.ASSET_DTB.fetch()
        rootfs_path = self.ASSET_ROOTFS.fetch()

        self.vm.set_console()
        self.vm.add_args('-bios', 'default',
                         '-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-drive', f'file={rootfs_path},format=raw,id=sd-card,snapshot=on',
                         '-device', 'sd-card,drive=sd-card',
                         '-append', 'console=ttyS0,115200 root=/dev/mmcblk0 rootwait earlycon')

        self.vm.launch()

        wait_for_console_pattern(self, 'Booting Linux on hartid')
        wait_for_console_pattern(self, 'Run /sbin/init as init process')

if __name__ == '__main__':
    LinuxKernelTest.main()
