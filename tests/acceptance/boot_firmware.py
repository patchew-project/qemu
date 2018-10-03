# coding=utf-8
#
# Functional test that boots SeaBIOS and checks the console
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Philippe Mathieu-Daudé <philmd@redhat.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import logging

from avocado_qemu import Test
from avocado.utils.wait import wait_for


def read_console_for_string(console, expected_string, logger, ignore_ansi=True):
    msg = console.readline()
    if len(msg) == 0:
        return False
    if logger:
        logger.debug(msg.strip() if ignore_ansi and not '\x1b' in msg else msg)
    return expected_string in msg


class BootFirmware(Test):
    """
    Boots a firmware and checks via a console it is operational

    :avocado: enable
    """

    def test_seabios(self):
        """
        Boots SeaBIOS on a default PC machine, checks the debug console

        :avocado: tags=arch:x86_64
        :avocado: tags=maxtime:5s
        :avocado: tags=quick
        """
        debugcon_path = os.path.join(self.workdir, 'debugconsole.log')
        serial_logger = logging.getLogger('serial')
        debugcon_logger = logging.getLogger('debugcon')

        self.vm.set_machine('pc')
        self.vm.set_console()
        self.vm.add_args('-nographic',
                         '-net', 'none',
                         '-global', 'isa-debugcon.iobase=0x402',
                         '-debugcon', 'file:%s' % debugcon_path)
        self.vm.launch()
        console = self.vm.console_socket.makefile()

        # serial console checks
        if not wait_for(read_console_for_string, timeout=5, step=0,
                        args=(console, 'No bootable device.', serial_logger)):
            self.fail("SeaBIOS failed to boot")

        # debug console checks
        expected = [
            'Running on QEMU (i440fx)',
            'Turning on vga text mode console',
            'Found 1 lpt ports',
            'Found 1 serial ports',
            'PS2 keyboard initialized',
        ]
        with open(debugcon_path) as debugcon:
            content = debugcon.readlines()
            for line in content: # TODO use FDDrainer
                debugcon_logger.debug(line.strip())
            for exp in expected:
                self.assertIn(exp + '\n', content)

    def test_ovmf_pc(self):
        """
        Boots OVMF on the default PC machine, checks the debug console

        :avocado: tags=arch:x86_64
        :avocado: tags=maxtime:10s
        """
        debugcon_path = os.path.join(self.workdir, 'debugconsole.log')
        serial_logger = logging.getLogger('serial')
        debugcon_logger = logging.getLogger('debugcon')

        self.vm.set_machine('pc')
        self.vm.set_console()
        self.vm.add_args('-nographic',
                         '-net', 'none',
                         '-global', 'isa-debugcon.iobase=0x402',
                         '-debugcon', 'file:%s' % debugcon_path,
                         '--bios', '/usr/share/OVMF/OVMF_CODE.fd')
        self.vm.launch()
        console = self.vm.console_socket.makefile()

        # serial console checks
        if not wait_for(read_console_for_string, timeout=10, step=0,
                        args=(console, 'EDK II', serial_logger)):
            self.fail("OVMF failed to boot")

        # debug console checks
        expected = [
            'SEC: Normal boot',
            'S3 support was detected on QEMU',
            'Platform PEI Firmware Volume Initialization',
            'DXE IPL Entry',
            'Installing FVB for EMU Variable support',
            'SmbiosCreateTable: Initialize 32-bit entry point structure',
            'PlatformBootManagerBeforeConsole',
            'OnRootBridgesConnected: root bridges have been connected, '
                'installing ACPI tables',
            'Found LPC Bridge device',
            'PlatformBootManagerAfterConsole',
            'EfiBootManagerConnectAll',
            '[Bds]Booting EFI Internal Shell',
        ]
        with open(debugcon_path) as debugcon:
            content = debugcon.readlines()
            for line in content: # TODO use FDDrainer
                debugcon_logger.debug(line.strip())
            for exp in expected:
                self.assertIn(exp + '\r\n', content)
