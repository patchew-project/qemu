#!/usr/bin/env python3
#
# Functional test that boots Zephyr on the Space Cubics SC-OBC Module A1
#
# Copyright (c) 2026 Space Cubics Inc.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import QemuSystemTest, Asset, wait_for_console_pattern


class ScobcA1Machine(QemuSystemTest):

    ASSET_HELLO_WORLD = Asset(
        'https://downloads.spacecubics.com/development/scobc-a1/qemu/'
        'zephyr-v4.5.0-hello_world.elf',
        '1578aebe742e4b3fc10af22994de4cd7feda3b54be19486a2efe42c099734c60')

    def test_scobc_a1_hello_world(self):
        self.set_machine('scobc-a1')
        kernel_path = self.ASSET_HELLO_WORLD.fetch()
        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path)
        self.vm.launch()
        wait_for_console_pattern(self, '*** Booting Zephyr OS build v4.5.0 ***')
        wait_for_console_pattern(self, 'Hello World! scobc_a1')


if __name__ == '__main__':
    QemuSystemTest.main()
