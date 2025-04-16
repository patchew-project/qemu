#!/usr/bin/env python3
#
# Test AmigaNG boards
#
# Copyright (c) 2023 BALATON Zoltan
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import subprocess

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern
from zipfile import ZipFile

class Pegasos2Machine(QemuSystemTest):

    timeout = 90

    ASSET_IMAGE = Asset(
        ('https://web.archive.org/web/20071021223056if_/http://www.bplan-gmbh.de/up050404/up050404'),
        '0b4ff042b293033e094b47ac7051824fc45f83adb340d455a17db1674b0150b0c60ffc624ac766f5369cd79f0447214d468baa182c1f18c5e04cd23a50f0b9a2')

    def test_ppc_pegasos2(self):
        self.require_accelerator("tcg")
        self.set_machine('pegasos2')
        file_path = self.ASSET_IMAGE.fetch()
        bios_fh = open(self.workdir + "/pegasos2.rom", "wb")
        subprocess.run(['tail', '-c','+85581', file_path], stdout=bios_fh)
        bios_fh.close()
        subprocess.run(['truncate', '-s', '524288', self.workdir + "/pegasos2.rom"], )

        self.vm.set_console()
        self.vm.add_args('-bios', self.workdir + '/pegasos2.rom')
        self.vm.launch()
        wait_for_console_pattern(self, 'SmartFirmware:')

    def test_ppc_pegasos2_test_tcg_crosspage_overflow_bug(self):
        self.require_accelerator("tcg")
        self.set_machine('pegasos2')
        file_path = self.ASSET_IMAGE.fetch()
        bios_fh = open(self.workdir + "/pegasos2.rom", "wb")
        subprocess.run(['tail', '-c','+85581', file_path], stdout=bios_fh)
        bios_fh.close()
        subprocess.run(['truncate', '-s', '524288', self.workdir + "/pegasos2.rom"], )

        with open(self.workdir + "/pegasos2.rom", "rb") as bios_fh:
            bios_data = bios_fh.read()
        
        # Patch the firmware image with the following instructions that will cause tcg to crash for 32-bit guests on 64-bit platforms:
        #   li r3, 0
        #   li r4, -1
        #   lwz r5, 0x0(r4)
        #   lwz r5, 0x0(r3)

        bios_data_new = bios_data[:0x6c10] + b'\x38\x60\x00\x00' + b'\x38\x80\xff\xff' + b'\x80\xa4\x00\x00' + b'\x80\xa3\x00\x00' + bios_data[0x6c20:]
        with open(self.workdir + "/pegasos2_new.rom", "wb") as bios_new_fh:
            bios_new_fh.write(bios_data_new)

        self.vm.set_console()
        self.vm.add_args('-bios', self.workdir + '/pegasos2_new.rom')
        self.vm.launch()
        wait_for_console_pattern(self, 'Releasing IDE reset')

        # set $pc = 0 and expect crash


if __name__ == '__main__':
    QemuSystemTest.main()
