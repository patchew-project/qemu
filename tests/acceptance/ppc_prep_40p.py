# Functional test that boots a PReP/40p machine and checks its serial console.
#
# Copyright (c) Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

import os
import logging

from avocado import skipIf
from avocado_qemu import Test


class IbmPrep40pMachine(Test):

    timeout = 60

    # TODO refactor to MachineTest
    def wait_for_console_pattern(self, success_message, failure_message=None):
        """
        Waits for messages to appear on the console, while logging the content

        :param success_message: if this message appears, test succeeds
        :param failure_message: if this message appears, test fails
        """
        console = self.vm.console_socket.makefile()
        console_logger = logging.getLogger('console')
        while True:
            msg = console.readline().strip()
            if not msg:
                continue
            console_logger.debug(msg)
            if success_message in msg:
                break
            if failure_message and failure_message in msg:
                fail = 'Failure message found in console: %s' % failure_message
                self.fail(fail)

    @skipIf(os.getenv('CONTINUOUS_INTEGRATION'), 'Running on Travis-CI')
    def test_factory_firmware_and_netbsd(self):
        """
        :avocado: tags=arch:ppc
        :avocado: tags=machine:40p
        :avocado: tags=slowness:high
        """
        bios_url = ('ftp://ftp.boulder.ibm.com/rs6000/firmware/'
                    '7020-40p/P12H0456.IMG')
        bios_hash = '1775face4e6dc27f3a6ed955ef6eb331bf817f03'
        bios_path = self.fetch_asset(bios_url, asset_hash=bios_hash)
        drive_url = ('https://ftp.netbsd.org/pub/NetBSD/NetBSD-archive/'
                     'NetBSD-4.0/prep/installation/floppy/generic_com0.fs')
        drive_hash = 'dbcfc09912e71bd5f0d82c7c1ee43082fb596ceb'
        drive_path = self.fetch_asset(drive_url, asset_hash=drive_hash)

        self.vm.set_machine('40p')
        self.vm.set_console()
        self.vm.add_args('-bios', bios_path,
                         '-fda', drive_path)
        self.vm.launch()
        os_banner = 'NetBSD 4.0 (GENERIC) #0: Sun Dec 16 00:49:40 PST 2007'
        self.wait_for_console_pattern(os_banner)
        self.wait_for_console_pattern('Model: IBM PPS Model 6015')

    def test_openfirmware(self):
        """
        :avocado: tags=arch:ppc
        :avocado: tags=machine:40p
        """
        bios_url = ('https://github.com/artyom-tarasenko/openfirmware/'
                    'releases/download/40p-20190413/q40pofw-serial.rom')
        bios_hash = '880c80172ea5b2247c0ac2a8bf36bbe385192c72'
        bios_path = self.fetch_asset(bios_url, asset_hash=bios_hash)

        self.vm.set_machine('40p')
        self.vm.set_console()
        self.vm.add_args('-bios', bios_path)

        self.vm.launch()
        self.wait_for_console_pattern('QEMU PReP/40p')
        fw_banner = 'Open Firmware, built  April 13, 2019 09:29:23'
        self.wait_for_console_pattern(fw_banner)
        prompt_msg = 'Type any key to interrupt automatic startup'
        self.wait_for_console_pattern(prompt_msg)

    def test_openbios_192m(self):
        """
        :avocado: tags=arch:ppc
        :avocado: tags=machine:40p
        """
        self.vm.set_machine('40p')
        self.vm.set_console()
        self.vm.add_args('-m', '192')

        self.vm.launch()
        self.wait_for_console_pattern('>> OpenBIOS')
        self.wait_for_console_pattern('>> Memory: 192M')
        self.wait_for_console_pattern('>> CPU type PowerPC,604')

    def test_openbios_and_netbsd(self):
        """
        :avocado: tags=arch:ppc
        :avocado: tags=machine:40p
        """
        drive_url = ('https://ftp.netbsd.org/pub/NetBSD/iso/7.1.2/'
                     'NetBSD-7.1.2-prep.iso')
        drive_hash = '78734c1bdda79778f0b6f391969ad2458ed8981c'
        drive_path = self.fetch_asset(drive_url, asset_hash=drive_hash)

        self.vm.set_machine('40p')
        self.vm.set_console()
        self.vm.add_args('-cdrom', drive_path,
                         '-boot', 'd')

        self.vm.launch()
        self.wait_for_console_pattern('NetBSD/prep BOOT, Revision 1.9')
