# Functional tests for the Lemote Fuloong-2E machine.
#
# Copyright (c) 2019 Philippe Mathieu-Daudé <f4bug@amsat.org>
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

from tesseract_utils import tesseract_available, tesseract_ocr

class MipsFuloong2e(Test):

    timeout = 60

    @skipUnless(os.getenv('AVOCADO_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
    @skipUnless(os.getenv('PMON2E_BIN_PATH'), 'PMON2E_BIN_PATH not available')
    def test_pmon_serial_console(self):
        """
        :avocado: tags=arch:mips64el
        :avocado: tags=machine:fuloong2e
        :avocado: tags=endian:little
        :avocado: tags=device:bonito64
        :avocado: tags=device:via686b
        """
        pmon_hash = 'c812e1695d7b2320036f3ef494976969' # v1.1.2
        pmon_path = self.fetch_asset('file://' + os.getenv('PMON2E_BIN_PATH'),
                                     asset_hash=pmon_hash, algorithm='md5')

        self.vm.set_console()
        self.vm.add_args('-bios', pmon_path)
        self.vm.launch()
        wait_for_console_pattern(self, 'PMON2000 MIPS Initializing. Standby...')
        wait_for_console_pattern(self, 'Booting Bios')

    @skipUnless(os.getenv('AVOCADO_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
    # Tesseract 4 adds a new OCR engine based on LSTM neural networks. The
    # new version is faster and more accurate than version 3. The drawback is
    # that it is still alpha-level software.
    @skipUnless(tesseract_available(4), 'tesseract v4 OCR tool not available')
    @skipUnless(os.getenv('PMON2E_BIN_PATH'), 'PMON2E_BIN_PATH not available')
    def test_pmon_framebuffer_console(self):
        """
        :avocado: tags=arch:mips64el
        :avocado: tags=machine:fuloong2e
        :avocado: tags=endian:little
        :avocado: tags=device:bonito64
        :avocado: tags=device:ati-vga
        """
        screenshot_path = os.path.join(self.workdir, 'dump.ppm')

        pmon_hash = 'c812e1695d7b2320036f3ef494976969' # v1.1.2
        pmon_path = self.fetch_asset('file://' + os.getenv('PMON2E_BIN_PATH'),
                                     asset_hash=pmon_hash, algorithm='md5')

        self.vm.set_console()
        self.vm.add_args('-bios', pmon_path,
                         '-vga', 'std',
                         '-device', 'ati-vga,model=rv100')
        self.vm.launch()

        wait_for_console_pattern(self, 'Video: Drawing the logo ...')
        self.log.info('VM launched, waiting for logo on display')
        time.sleep(2)
        wait_for_console_pattern(self, 'CONSOLE_SIZE')
        self.vm.command('human-monitor-command', command_line='stop')
        self.vm.command('human-monitor-command',
                        command_line='screendump %s' % screenshot_path)

        lines = tesseract_ocr(screenshot_path, tesseract_version=4,
                              tesseract_args='--dpi 128')
        pmon_version = 'PNON2000 for Loongson, Version 1.1.2' # PNON is enough
        self.assertIn(pmon_version, lines)

    @skipUnless(os.getenv('AVOCADO_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
    @skipUnless(os.getenv('RESCUE_YL_PATH'), 'RESCUE_YL_PATH not available')
    def test_linux_kernel_isa_serial(self):
        """
        :avocado: tags=arch:mips64el
        :avocado: tags=machine:fuloong2e
        :avocado: tags=endian:little
        :avocado: tags=device:bonito64
        :avocado: tags=device:via686b
        """
        # Recovery system for the Yeeloong laptop
        # (enough to test the fuloong2e southbridge, accessing its ISA bus)
        # http://dev.lemote.com/files/resource/download/rescue/rescue-yl
        kernel_hash = 'ec4d1bd89a8439c41033ca63db60160cc6d6f09a'
        kernel_path = self.fetch_asset('file://' + os.getenv('RESCUE_YL_PATH'),
                                       asset_hash=kernel_hash)

        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path)
        self.vm.launch()
        wait_for_console_pattern(self, 'Linux version 2.6.27.7lemote')
        cpu_revision = 'CPU revision is: 00006302 (ICT Loongson-2)'
        wait_for_console_pattern(self, cpu_revision)
