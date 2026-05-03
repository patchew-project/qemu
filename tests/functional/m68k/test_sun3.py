# SPDX-License-Identifier: GPL-2.0-or-later
#!/usr/bin/env python3
#
# Functional test that boots a Sun-3 machine and checks its serial console.
#
# Copyright (c) 2026
#


import os
from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern

class Sun3Machine(QemuSystemTest):
    timeout = 60

    ASSET_PROM = Asset(
        'https://github.com/54weasels/sun3_60/raw/main/bootprom/boot_1810_01.BIN',
        '471bf34cc9313814726c2e3506d85a13291603e37b45aaffa71bc1154a2d4851'
    )

    def test_m68k_sun3_boot(self):
        self.set_machine('sun3')
        prom_path = self.ASSET_PROM.fetch()

        # Sun-3 maps TTY A to serial_hd(3)
        self.vm.set_console(console_index=3)
        self.vm.add_args('-bios', prom_path)
        self.vm.launch()

        wait_for_console_pattern(self, '>')

if __name__ == '__main__':
    QemuSystemTest.main()
