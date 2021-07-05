# Functional tests for the Jazz machines.
#
# Copyright (c) 2021 Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import lzma
import shutil

from avocado import skipUnless
from avocado_qemu import Test
from avocado_qemu import exec_command
from avocado_qemu import exec_command_and_wait_for_pattern
from avocado_qemu import interrupt_interactive_console_until_pattern
from avocado_qemu import wait_for_console_pattern

from tesseract_utils import tesseract_available, tesseract_ocr

class MipsJazz(Test):

    timeout = 60

    @skipUnless(os.getenv('NTPROM_BIN_PATH'), 'NTPROM_BIN_PATH not available')
    def test_magnum_netbsd_5_1(self):
        """
        :avocado: tags=arch:mips64el
        :avocado: tags=machine:magnum
        :avocado: tags=os:netbsd
        :avocado: tags=device:sonic
        :avocado: tags=device:esp
        """
        drive_url = ('http://archive.netbsd.org/pub/NetBSD-archive/'
                     'NetBSD-5.1/iso/arccd-5.1.iso')
        drive_hash = ('c91a57fb373636247d1f1ce283a610ba529e208604a'
                      'f2a9e0237551fb3d25459c7697775af8c8d35a9764e'
                      'fca87cfb591f363643e93417cfdb8857215ceb405e')
        drive_path = self.fetch_asset(drive_url, asset_hash=drive_hash,
                                      algorithm='sha512')
        ntprom_hash = '316de17820192c89b8ee6d9936ab8364a739ca53'
        ntprom_path = self.fetch_asset('file://' + os.getenv('NTPROM_BIN_PATH'),
                                       asset_hash=ntprom_hash, algorithm='sha1')
        nvram_size = 8200
        nvram_path = 'nvram.bin'
        nvram_xz_hash = '3d4565124ff2369706b97e1d0ef127a68c23d418'
        nvram_xz_path = os.path.dirname(os.path.abspath(__file__)) \
                        + '/machine_mips_jazz.d/nvram.bin.xz'
        nvram_xz_path = self.fetch_asset('file://' + nvram_xz_path,
                                         asset_hash=nvram_xz_hash,
                                         algorithm='sha1')

        with lzma.open(nvram_xz_path, 'rb') as f_in:
            with open(nvram_path, 'wb') as f_out:
                shutil.copyfileobj(f_in, f_out)
                f_out.seek(nvram_size)
                f_out.write(b'\0')

        self.vm.set_console()
        self.vm.add_args('-bios', ntprom_path,
                         '-drive', 'if=scsi,unit=2,media=cdrom,format=raw,file='
                                   + drive_path,
                         '-global', 'ds1225y.filename=' + nvram_path,
                         '-global', 'ds1225y.size=' + str(nvram_size),
                         '-nic', 'user,model=dp83932,mac=00:00:00:02:03:04')
        self.vm.launch()

        console_pattern = 'ARC Multiboot Version 174 (SGI Version 2.6)'
        wait_for_console_pattern(self, console_pattern)

        wait_for_console_pattern(self, 'Use the arrow keys to select.')
        exec_command(self, '\x1b[B') # Cursor Control 'Down'

        program = 'scsi(0)cdrom(2)fdisk(0)boot scsi(0)cdrom(2)fdisk(0)netbsd'
        exec_command(self, program)
        wait_for_console_pattern(self, 'NetBSD/arc Bootstrap, Revision 1.1')

        # Terminal type? [vt100]
        console_pattern = 'erase ^H, werase ^W, kill ^U, intr ^C, status ^T'
        wait_for_console_pattern(self, console_pattern)

        # (I)nstall, (S)hell or (H)alt
        exec_command_and_wait_for_pattern(self, '', 'Erase is backspace.')
        exec_command(self, 'S')
        interrupt_interactive_console_until_pattern(self, '#')

        exec_command(self, 'ifconfig sn0 10.0.2.3/24')
        interrupt_interactive_console_until_pattern(self, '#')

        exec_command_and_wait_for_pattern(self, 'ping -c 3 10.0.2.2',
                '3 packets transmitted, 3 packets received, 0.0% packet loss')

        exec_command_and_wait_for_pattern(self, 'shutdown -r now',
                                          'rebooting...')
