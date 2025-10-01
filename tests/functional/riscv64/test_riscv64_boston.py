#!/usr/bin/env python3
#
# Boston board test for RISC-V P8700 processor by MIPS
#
# Copyright (c) 2025 MIPS
#
# SPDX-License-Identifier: GPL-2.0-or-later
#

import os
from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern

class RiscvBostonTest(QemuSystemTest):
    """
    Test the boston-aia board with P8700 processor
    """

    timeout = 120  # Timeout for boot tests

    ASSET_FW_PAYLOAD = Asset(
        'https://github.com/MIPS/linux-test-downloads/raw/main/p8700/fw_payload.bin',
        'd6f4ae14d0c178c1d0bb38ddf64557536ca8602a588b220729a8aa17caa383aa')

    ASSET_ROOTFS = Asset(
        'https://github.com/MIPS/linux-test-downloads/raw/main/p8700/rootfs.ext2',
        'f937e21b588f0d1d17d10a063053979686897bbbbc5e9617a5582f7c1f48e565')

    def _fetch_rootfs(self):
        """Fetch rootfs and fix permissions"""
        rootfs_path = self.ASSET_ROOTFS.fetch()
        # Ensure the rootfs file is readable
        try:
            os.chmod(rootfs_path, 0o644)
        except:
            pass  # If we can't change permissions, try anyway
        return rootfs_path

    def test_boston_boot_linux(self):
        """
        Test full Linux kernel boot with rootfs on Boston board
        """
        self.set_machine('boston-aia')
        fw_payload_path = self.ASSET_FW_PAYLOAD.fetch()
        rootfs_path = self._fetch_rootfs()

        self.vm.add_args('-cpu', 'mips-p8700')
        self.vm.add_args('-m', '2G')
        self.vm.add_args('-smp', '2')
        self.vm.add_args('-kernel', fw_payload_path)
        self.vm.add_args('-drive', f'file={rootfs_path},format=raw')

        self.vm.set_console()
        self.vm.launch()

        # Wait for OpenSBI
        wait_for_console_pattern(self, 'OpenSBI')

        # Wait for Linux kernel boot
        wait_for_console_pattern(self, 'Linux version')
        wait_for_console_pattern(self, 'Machine model: MIPS P8700')

        # Test e1000e network card functionality
        wait_for_console_pattern(self, 'e1000e')
        wait_for_console_pattern(self, 'Network Connection')

        # Wait for boot to complete - system reaches login prompt
        wait_for_console_pattern(self, 'Run /sbin/init as init process')

    def test_boston_7_vps_boot_linux(self):
        """
        Test full Linux kernel boot with rootfs on Boston board
        """
        self.set_machine('boston-aia')
        fw_payload_path = self.ASSET_FW_PAYLOAD.fetch()
        rootfs_path = self._fetch_rootfs()

        self.vm.add_args('-cpu', 'mips-p8700')
        self.vm.add_args('-m', '2G')
        self.vm.add_args('-smp', '7')
        self.vm.add_args('-kernel', fw_payload_path)
        self.vm.add_args('-drive', f'file={rootfs_path},format=raw')

        self.vm.set_console()
        self.vm.launch()

        # Wait for OpenSBI
        wait_for_console_pattern(self, 'OpenSBI')

        # Wait for Linux kernel boot
        wait_for_console_pattern(self, 'Linux version')
        wait_for_console_pattern(self, 'Machine model: MIPS P8700')

        # Test e1000e network card functionality
        wait_for_console_pattern(self, 'e1000e')
        wait_for_console_pattern(self, 'Network Connection')

        # Wait for boot to complete - system reaches login prompt
        wait_for_console_pattern(self, 'Run /sbin/init as init process')

    def test_boston_35_vps_boot_linux(self):
        """
        Test full Linux kernel boot with rootfs on Boston board
        """
        self.set_machine('boston-aia')
        fw_payload_path = self.ASSET_FW_PAYLOAD.fetch()
        rootfs_path = self._fetch_rootfs()

        self.vm.add_args('-cpu', 'mips-p8700')
        self.vm.add_args('-m', '2G')
        self.vm.add_args('-smp', '35')
        self.vm.add_args('-kernel', fw_payload_path)
        self.vm.add_args('-drive', f'file={rootfs_path},format=raw')

        self.vm.set_console()
        self.vm.launch()

        # Wait for OpenSBI
        wait_for_console_pattern(self, 'OpenSBI')

        # Wait for Linux kernel boot
        wait_for_console_pattern(self, 'Linux version')
        wait_for_console_pattern(self, 'Machine model: MIPS P8700')

        # Test e1000e network card functionality
        wait_for_console_pattern(self, 'e1000e')
        wait_for_console_pattern(self, 'Network Connection')

        # Wait for boot to complete - system reaches login prompt
        wait_for_console_pattern(self, 'Run /sbin/init as init process')

    def test_boston_65_vps_boot_linux(self):
        """
        Test that 65 CPUs is rejected as invalid (negative test case)
        """
        from subprocess import run, PIPE

        fw_payload_path = self.ASSET_FW_PAYLOAD.fetch()
        rootfs_path = self._fetch_rootfs()

        cmd = [
            self.qemu_bin,
            '-M', 'boston-aia',
            '-cpu', 'mips-p8700',
            '-m', '2G',
            '-smp', '65',
            '-kernel', fw_payload_path,
            '-drive', f'file={rootfs_path},format=raw',
            '-nographic'
        ]

        # Run QEMU and expect it to fail immediately.
        result = run(cmd, capture_output=True, text=True, timeout=5)

        # Check that QEMU exited with error code 1
        self.assertEqual(result.returncode, 1,
                         "QEMU should exit with code 1 for invalid SMP count")

        # Check error message
        self.assertIn('Invalid SMP CPUs 65', result.stderr,
                      "Error message should indicate invalid SMP CPU count")

if __name__ == '__main__':
    QemuSystemTest.main()
