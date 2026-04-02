#!/usr/bin/env python3
#
# Functional test that boots a kernel and checks the console
#
# Based on test_arm_generic_fdt_alpine.py
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import QemuSystemTest, Asset, skipSlowTest
from qemu_test import wait_for_console_pattern
from test_arm_generic_fdt import fetch_firmware

from pathlib import Path

class Aarch64ArmGenericFdtAlpine(QemuSystemTest):

    ASSET_ALPINE_ISO = Asset(
        ('https://dl-cdn.alpinelinux.org/'
         'alpine/v3.17/releases/aarch64/alpine-standard-3.17.2-aarch64.iso'),
        '5a36304ecf039292082d92b48152a9ec21009d3a62f459de623e19c4bd9dc027')

    current_dir = Path(__file__).resolve().parent

    hw_dtb_path = current_dir.parent.parent/ "data" / "dtb" / "aarch64" / \
        "arm-generic-fdt" / "arm64-sbsa-hw.dtb"

    dtb_path = current_dir.parent.parent/ "data" / "dtb" / "aarch64" / \
        "arm-generic-fdt" / "arm64-sbsa-guest.dtb"


    # This tests the whole boot chain from EFI to Userspace
    # We only boot a whole OS for the current top level CPU and GIC
    # Other test profiles should use more minimal boots
    def boot_alpine_linux(self):
        self.set_machine('arm-generic-fdt')

        fetch_firmware(self)
        iso_path = self.ASSET_ALPINE_ISO.fetch()

        self.vm.set_console()
        self.vm.add_args('-machine', f'hw-dtb={self.hw_dtb_path}')
        self.vm.add_args('-dtb', str(self.dtb_path))
        self.vm.add_args(
            "-device", f"ide-cd,bus=ahci.0,unit=0,drive=cdrom0",
        )
        self.vm.add_args(
            "-drive", f"file={iso_path},if=none,id=cdrom0",
        )
        self.vm.add_args('-device', "bochs-display")
        self.vm.add_args('-netdev', "user,id=net0")
        self.vm.add_args('-device', "e1000e,netdev=net0")

        self.vm.launch()
        wait_for_console_pattern(self, "Welcome to Alpine Linux 3.17")

    def test_alpine_linux(self):
        self.boot_alpine_linux()


if __name__ == '__main__':
    QemuSystemTest.main()
