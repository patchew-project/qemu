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
import gzip
import time
import shutil
import logging
import tempfile

from avocado_qemu import Test


class BootFirmwareX86(Test):
    """
    Boots a firmware on a default PC machine and checks the debug console is
    operational

    :avocado: enable
    :avocado: tags=x86_64,quick
    """

    timeout = 15

    def test_seabios(self):
        tmpdirname = tempfile.mkdtemp()
        debugcon_path = os.path.join(tmpdirname, 'debugconsole.log')
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
        timeout = time.time() + 5
        while True:
            msg = console.readline()
            if not '\x1b' in msg: # ignore ANSI sequences
                serial_logger.debug(msg.strip())
            if 'No bootable device.' in msg:
                break
            if time.time() > timeout:
                self.fail('SeaBIOS failed to boot?')

        # debug console checks
        expected = [
            'Running on QEMU (i440fx)',
            'Turning on vga text mode console',
            'Found 1 lpt ports',
            'Found 1 serial ports',
            'PS2 keyboard initialized',
        ]
        lines = open(debugcon_path).readlines()
        for msg in lines:
            debugcon_logger.debug(msg.strip())
        for line in expected:
            if line + '\n' not in lines:
                self.fail('missing: %s' % line)
        shutil.rmtree(tmpdirname, ignore_errors=True)

    def test_ovmf(self):
        tmpdirname = tempfile.mkdtemp()
        debugcon_path = os.path.join(tmpdirname, 'debugconsole.log')
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
        timeout = time.time() + 15
        while True:
            msg = console.readline()
            if not '\x1b' in msg: # ignore ANSI sequences
                serial_logger.debug(msg.strip())
            if 'EDK II' in msg:
                break
            if time.time() > timeout:
                self.fail('OVMF failed to boot?')

        # debug console checks
        expected = [
            'SEC: Normal boot',
            'S3 support was detected on QEMU',
            'Platform PEI Firmware Volume Initialization',
            'DXE IPL Entry',
            'Installing FVB for EMU Variable support',
            'DetectSmbiosVersion: SMBIOS version from QEMU: 0x0208',
            'SmbiosCreateTable: Initialize 32-bit entry point structure',
            'PlatformBootManagerBeforeConsole',
            'OnRootBridgesConnected: root bridges have been connected, '
                'installing ACPI tables',
            'Found LPC Bridge device',
            'PlatformBootManagerAfterConsole',
            'EfiBootManagerConnectAll',
            '[Bds]Booting EFI Internal Shell',
        ]
        lines = open(debugcon_path).readlines()
        for msg in lines:
            debugcon_logger.debug(msg.strip())
        for line in expected:
            if line + '\r\n' not in lines:
                self.fail('missing: %s' % line)
        shutil.rmtree(tmpdirname, ignore_errors=True)


class BootFirmwareAarch64(Test):
    """
    Boots the EDK2 firmware on a default virt machine and checks the console is
    operational

    :avocado: enable
    :avocado: tags=arch:aarch64
    :avocado: tags=aarch64,quick
    """

    timeout = 15

    def test_aavmf(self):
        tmpdirname = tempfile.mkdtemp()
        image_url = ('http://snapshots.linaro.org/components/kernel/'
                    'leg-virt-tianocore-edk2-upstream/latest/'
                    'QEMU-AARCH64/DEBUG_GCC5/QEMU_EFI.img.gz')
        image_path_gz = self.fetch_asset(image_url)
        image_path = os.path.join(tmpdirname, 'flash.img')
        with gzip.open(image_path_gz) as gz, open(image_path, 'wb') as img:
            shutil.copyfileobj(gz, img)

        self.vm.set_machine('virt')
        self.vm.set_console()
        self.vm.add_args('-nographic',
                         '-cpu', 'cortex-a57',
                         '-m', '1G',
                         '-pflash', image_path)
        self.vm.launch()
        console = self.vm.console_socket.makefile()
        serial_logger = logging.getLogger('serial')

        # serial console checks
        while True:
            msg = console.readline()
            if not '\x1b' in msg: # ignore ANSI sequences
                serial_logger.debug(msg.strip())
            if 'Start PXE over IPv4InstallProtocolInterface:' in msg:
                break
        shutil.rmtree(tmpdirname, ignore_errors=True)
