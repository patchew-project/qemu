# Functional test that boots a VM and run OCR on the framebuffer
#
# Copyright (c) Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import logging
import time
import distutils.spawn

from avocado import skipUnless
from avocado_qemu import Test
from avocado.utils import process


class NextCubeMachine(Test):

    timeout = 15

    @skipUnless(distutils.spawn.find_executable('tesseract'),
                'tesseract OCR tool not installed')
    def test_bootrom_framebuffer(self):
        """
        :avocado: tags=arch:m68k
        :avocado: tags=machine:next-cube
        :avocado: tags=device:framebuffer
        """
        rom_url = ('http://www.nextcomputers.org/NeXTfiles/Software/ROM_Files/'
                   '68040_Non-Turbo_Chipset/Rev_2.5_v66.BIN')
        rom_hash = 'b3534796abae238a0111299fc406a9349f7fee24'
        rom_path = self.fetch_asset(rom_url, asset_hash=rom_hash)

        self.vm.set_machine('next-cube')
        self.vm.add_args('-bios', rom_path)
        self.vm.launch()

        self.log.info('VM launched, waiting for display')
        # FIXME how to catch the 'displaysurface_create 1120x832' trace-event?
        time.sleep(2)

        screenshot_path = self.workdir + "dump"
        self.vm.command('human-monitor-command',
                        command_line='screendump %s' % screenshot_path)

        console_logger = logging.getLogger('ocr')
        proc = process.run("tesseract %s stdout" % screenshot_path)
        console_logger.debug(proc.stdout_text)
        self.assertIn('Backplane', proc.stdout_text)
        self.assertIn('Ethernet address', proc.stdout_text)
