# Functional tests for the Generic Loongson-3 Platform.
#
# Copyright (c) 2020 Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import time

from avocado import skipUnless
from avocado_qemu import Test
from avocado_qemu import wait_for_console_pattern

class MipsFuloong3(Test):

    timeout = 60

    @skipUnless(os.getenv('PMON_PATH'), 'PMON_PATH not available')
    @skipUnless(os.getenv('AVOCADO_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
    def test_pmon_BLD_serial_console(self):
        """
        :avocado: tags=arch:mips64el
        :avocado: tags=endian:little
        :avocado: tags=machine:loongson3-virt
        :avocado: tags=cpu:Loongson-3A1000
        :avocado: tags=device:liointc
        :avocado: tags=device:goldfish_rtc
        """
        pmon_name = 'pmon_BLD-3A3000-780EMATX-1w-V1.10.bin'
        pmon_hash = '38916ee03ed09a86997b40c687c83e92'
        pmon_path = self.fetch_asset('file://' + os.path.join(
                                        os.getenv('PMON_PATH'), pmon_name),
                                     asset_hash=pmon_hash, algorithm='md5')

        self.vm.set_console()
        self.vm.add_args('-bios', pmon_path)
        self.vm.launch()
        wait_for_console_pattern(self, 'PMON2000 MIPS Initializing. Standby...')
        wait_for_console_pattern(self, 'Shut down other cores')
        wait_for_console_pattern(self, 'Waiting HyperTransport bus to be up.')

    @skipUnless(os.getenv('PMON_PATH'), 'PMON_PATH not available')
    @skipUnless(os.getenv('AVOCADO_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
    def test_pmon_A1101_serial_console(self):
        """
        :avocado: tags=arch:mips64el
        :avocado: tags=endian:little
        :avocado: tags=machine:loongson3-virt
        :avocado: tags=cpu:Loongson-3A1000
        :avocado: tags=device:liointc
        :avocado: tags=device:goldfish_rtc
        """
        pmon_name = 'pmon-A1101-2.0.8.bin'
        pmon_hash = 'cc40276213cfa20922720f183b92ab61'
        pmon_path = self.fetch_asset('file://' + os.path.join(
                                        os.getenv('PMON_PATH'), pmon_name),
                                     asset_hash=pmon_hash, algorithm='md5')

        self.vm.set_console()
        self.vm.add_args('-bios', pmon_path)
        self.vm.launch()
        wait_for_console_pattern(self, 'PMON2000 MIPS Initializing. Standby...')
        wait_for_console_pattern(self, 'godson2 caches found')
