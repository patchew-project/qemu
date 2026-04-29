#!/usr/bin/env python3
#
# Functional tests for the PIC32MK board emulation
#
# Copyright (c) 2026 QEMU contributors
# SPDX-License-Identifier: GPL-2.0-or-later

import os

from qemu_test import QemuSystemTest
from qemu_test import wait_for_console_pattern


class PIC32MKMachine(QemuSystemTest):
    """
    Tests for the PIC32MK MCU board (-M pic32mk).

    The FreeRTOS firmware is expected to be pre-built at
    tests/freertos/hello_freertos.bin relative to the source tree.
    """

    timeout = 30

    def get_firmware_path(self):
        """Locate the pre-built FreeRTOS firmware binary."""
        # data_file() resolves relative to tests/functional/
        # We need to go up two levels to reach the source root
        src_root = os.path.join(
            os.path.dirname(__file__), '..', '..', '..')
        fw_path = os.path.realpath(
            os.path.join(src_root, 'tests', 'freertos', 'hello_freertos.bin'))
        if not os.path.isfile(fw_path):
            self.skipTest(
                f'FreeRTOS firmware not built: {fw_path}\n'
                'Run: make -C tests/freertos')
        return fw_path

    def test_boot_message(self):
        """PIC32MK boots and prints the FreeRTOS banner."""
        self.set_machine('pic32mk')
        self.vm.set_console()
        self.vm.add_args('-bios', self.get_firmware_path(),
                         '-nographic')
        self.vm.launch()
        wait_for_console_pattern(self,
                                 'PIC32MK QEMU booting FreeRTOS...')

    def test_freertos_hello_task(self):
        """FreeRTOS scheduler starts and the Hello task fires."""
        self.set_machine('pic32mk')
        self.vm.set_console()
        self.vm.add_args('-bios', self.get_firmware_path(),
                         '-nographic')
        self.vm.launch()
        # First: boot banner
        wait_for_console_pattern(self,
                                 'PIC32MK QEMU booting FreeRTOS...')
        # Second: at least one Hello message (proves Timer1 tick + UART TX IRQ)
        wait_for_console_pattern(self,
                                 'Hello from FreeRTOS!')

    def test_freertos_recurring_ticks(self):
        """Timer1 delivers recurring ticks — multiple Hello messages appear."""
        self.set_machine('pic32mk')
        self.vm.set_console()
        self.vm.add_args('-bios', self.get_firmware_path(),
                         '-nographic')
        self.vm.launch()
        wait_for_console_pattern(self,
                                 'PIC32MK QEMU booting FreeRTOS...')
        # Wait for a second Hello — proves the tick ISR fires repeatedly
        # and context switches work (CS0 yield + Timer1 preemption).
        wait_for_console_pattern(self,
                                 'Hello from FreeRTOS!')
        wait_for_console_pattern(self,
                                 'Hello from FreeRTOS!')

    def test_freertos_ping_task(self):
        """Second FreeRTOS task (Ping) also runs — proves multitasking."""
        self.set_machine('pic32mk')
        self.vm.set_console()
        self.vm.add_args('-bios', self.get_firmware_path(),
                         '-nographic')
        self.vm.launch()
        wait_for_console_pattern(self,
                                 'PIC32MK QEMU booting FreeRTOS...')
        # Ping task fires every 5 seconds
        wait_for_console_pattern(self, 'Ping')


if __name__ == '__main__':
    QemuSystemTest.main()
