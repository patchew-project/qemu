#!/usr/bin/env python3
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
"""
riscv-server-ref board test
"""

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern

class RiscvServerRefTest(QemuSystemTest):
    """
    Test the riscv-server-ref board
    """

    ASSET_KERNEL = Asset(
        ('https://github.com/danielhb/qemu-machine-boot/raw/refs/heads/'
         'master/riscv/images/virt64/buildroot/Image'),
        '6bacc876c769c1bb6057d2bf549eba67fbe83916e8223f9fe21c8e8fff665a36')

    ASSET_ROOTFS = Asset(
        ('https://github.com/danielhb/qemu-machine-boot/raw/refs/heads/'
         'master/riscv/images/virt64/buildroot/rootfs.ext2'),
        'f00bb88749f945d80675540a1338bd1ccb226574685a5b6c65ab44027d0411a8')

    def test_boot_linux_test(self):
        self.set_machine('riscv-server-ref')
        kernel_path = self.ASSET_KERNEL.fetch()
        rootfs_path = self.ASSET_ROOTFS.fetch()

        self.vm.add_args('-kernel', kernel_path)
        self.vm.add_args('-append', 'rw rootwait root=/dev/sda')
        self.vm.add_args('-drive',
            f'file={rootfs_path},format=raw,id=hd0,snapshot=on,if=none')
        self.vm.add_args('-device', 'ahci,id=ahci')
        self.vm.add_args('-device', 'ide-hd,drive=hd0,bus=ahci.0')

        self.vm.set_console()
        self.vm.launch()

        # Wait for OpenSBI
        wait_for_console_pattern(self, 'OpenSBI')

        # Wait for Linux kernel boot
        wait_for_console_pattern(self, 'Linux version')
        wait_for_console_pattern(self, 'Machine model: qemu,riscv-server-ref')

        # Test e1000e network card functionality
        wait_for_console_pattern(self, 'e1000e')
        wait_for_console_pattern(self, 'Network Connection')

        # Wait for boot to complete - system reaches login prompt
        wait_for_console_pattern(self, 'Run /sbin/init as init process')

if __name__ == '__main__':
    QemuSystemTest.main()
