#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Functional test for the Microchip PIC32MK GPK/MCM board emulation.
#
# The test firmware (pic32mk_test_fw.S) is a minimal MIPS assembly program
# that initialises UART1 and writes "Hello, PIC32MK!\r\n" then spins.
# This verifies that the board boots, the CPU executes code, and UART1 TX
# produces output on the emulated serial console.

import os

from qemu_test import QemuSystemTest
from qemu_test import wait_for_console_pattern


def _fw_path():
    """Return the path to the pre-built test firmware binary."""
    build_dir = os.environ.get('BUILD_DIR', '')
    if build_dir:
        candidate = os.path.join(build_dir,
                                 'tests', 'functional', 'mipsel',
                                 'pic32mk_test_fw.bin')
        if os.path.isfile(candidate):
            return candidate
    # Fallback: look next to this file (for out-of-tree runs)
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(here, 'pic32mk_test_fw.bin')


class PIC32MKMachine(QemuSystemTest):
    """
    Boot test for the Microchip PIC32MK GPK/MCM board (-M pic32mk).

    Requires pic32mk_test_fw.bin built from pic32mk_test_fw.S by meson.
    The binary must be present in the meson build tree under
    tests/functional/mipsel/pic32mk_test_fw.bin.
    """

    timeout = 30

    def test_uart_boot_message(self):
        """Board boots and UART1 TX prints the expected banner."""
        fw = _fw_path()
        if not os.path.isfile(fw):
            self.skipTest(
                'pic32mk_test_fw.bin not found — '
                'build with: ninja -C <builddir> pic32mk_test_fw.bin')
        self.set_machine('pic32mk')
        self.vm.set_console()
        self.vm.add_args('-bios', fw, '-nographic')
        self.vm.launch()
        wait_for_console_pattern(self, 'Hello, PIC32MK!')


if __name__ == '__main__':
    QemuSystemTest.main()
