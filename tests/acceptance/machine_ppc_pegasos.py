# Functional tests for the Pegasos2 machine.
#
# Copyright (c) 2021 Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import time

from avocado import skipUnless
from avocado_qemu import Test
from avocado_qemu import exec_command_and_wait_for_pattern
from avocado_qemu import wait_for_console_pattern
from tesseract_utils import tesseract_available, tesseract_ocr

PIL_AVAILABLE = True
try:
    from PIL import Image
except ImportError:
    PIL_AVAILABLE = False


@skipUnless(os.getenv('AVOCADO_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
@skipUnless(os.getenv('PEGASOS2_ROM_PATH'), 'PEGASOS2_ROM_PATH not available')
class PPCPegasos2(Test):
    timeout = 60

    def test_rom_serial_console(self):
        """
        :avocado: tags=arch:ppc
        :avocado: tags=machine:pegasos2
        :avocado: tags=device:mv64361
        :avocado: tags=device:vt8231
        """
        rom_hash = '3248e02596480f2dba5944bd219ecfad'
        rom_path = self.fetch_asset('file://' + os.getenv('PEGASOS2_ROM_PATH'),
                                    asset_hash=rom_hash, algorithm='md5')

        self.vm.set_console()
        self.vm.add_args('-bios', rom_path)
        self.vm.launch()
        msg = 'PegasosII Boot Strap (c) 2002-2003 bplan GmbH'
        wait_for_console_pattern(self, msg)
        msg = 'Welcome to SmartFirmware(tm) for bplan Pegasos2 version 1.1'
        wait_for_console_pattern(self, msg)

    def test_morphos_cdrom_vga(self):
        """
        :avocado: tags=arch:ppc
        :avocado: tags=machine:pegasos2
        :avocado: tags=device:mv64361
        :avocado: tags=device:vt8231
        :avocado: tags=device:ati-vga
        """
        rom_hash = '3248e02596480f2dba5944bd219ecfad'
        rom_path = self.fetch_asset('file://' + os.getenv('PEGASOS2_ROM_PATH'),
                                    asset_hash=rom_hash, algorithm='md5')
        iso_url = 'https://www.morphos-team.net/morphos-3.15.iso'
        iso_hash = 'a19dbfbbc4728e0ba9ceb6335db69ca4'
        iso_path = self.fetch_asset(iso_url,
                                    asset_hash=iso_hash, algorithm='md5')

        self.vm.set_console()
        self.vm.add_args('-bios', rom_path,
                         '-device', 'ati-vga,romfile=',
                         '-cdrom', iso_path)
        self.vm.launch()
        msg = 'Welcome to SmartFirmware(tm) for bplan Pegasos2 version 1.1'
        wait_for_console_pattern(self, msg)
        wait_for_console_pattern(self, 'entering main read/eval loop...')
        msg = 'filesystem:  System-ID: "MORPHOS"  Volume-ID: "MorphOSBoot"'
        exec_command_and_wait_for_pattern(self, 'boot cd boot.img', msg)

        msg = 'ide.device@2:  CDRom <CD001>,<MORPHOS > found, bootable'
        wait_for_console_pattern(self, msg)
        if PIL_AVAILABLE:
            delay_s = 20 if tesseract_available(4) else 8

            self.log.info('VM launched, waiting for display')
            # TODO: Use avocado.utils.wait.wait_for to catch the
            #       'displaysurface_create 1120x832' trace-event.
            time.sleep(delay_s)

            screenshot_path = os.path.join(self.workdir, "dump.ppm")
            self.vm.command('human-monitor-command',
                            command_line='screendump %s' % screenshot_path)
            width, height = Image.open(screenshot_path).size
            self.assertEqual(width, 2048)
            self.assertEqual(height, 1152)

            if tesseract_available(4):
                lines = tesseract_ocr(screenshot_path, tesseract_version=4)
                text = '\n'.join(lines)
                msg = 'American keyboard with Greek input extension, 105 keys'
                self.assertIn(msg, text)
