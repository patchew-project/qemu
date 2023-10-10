# Functional test that boots MacOS on ppc mac99
#
# Copyright (c) 2023 IBM Corporation
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import time
import tempfile
import filecmp
import os

from typing import List
from avocado.utils import archive
from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern

class PPCMacOSTest(QemuSystemTest):

    timeout = 240

    def test_macos9(self):
        """
        :avocado: tags=arch:ppc
        :avocado: tags=machine:mac99
        :avocado: tags=device:framebuffer
        """

        basedir = os.getenv('AVOCADO_TEST_BASEDIR')
        screenshot_path = os.path.join(basedir, 'ppc/macos9.ppm')

        image = os.getenv('MACOS9_IMAGE')
        if not image:
            self.cancel('No MACOS9_IMAGE environment variable defined')
        drive_path = self.fetch_asset(image)

        self.vm.set_console()
        self.vm.add_args('-M', 'via=pmu')
        self.vm.add_args('-m', '1g')
        self.vm.add_args('-cpu', 'G4')
        self.vm.add_args('-drive', 'file=' + drive_path)
#        self.vm.add_args('-net', 'nic,model=rtl8139')
#        self.vm.add_args('-net', 'user,hostfwd=:127.0.0.1:2223-:22,hostname=qemu')
        self.vm.add_args('-vga', 'std')
        self.vm.add_args('-g', '640x480')
        self.vm.launch()

        # This comes up after 60 seconds on a fast x86 CPU. Give it
        # 180s to be sure.
        time.sleep(180)
        with tempfile.NamedTemporaryFile(suffix='.ppm',
                                         prefix='screendump-', delete=False) as ppmfile:
            self.vm.command('screendump', filename=ppmfile.name)
            if not filecmp.cmp(ppmfile.name, screenshot_path):
                self.fail('Screenshot does not match')

    def test_macos10(self):
        """
        :avocado: tags=arch:ppc
        :avocado: tags=machine:mac99
        :avocado: tags=device:framebuffer
        """

        basedir = os.getenv('AVOCADO_TEST_BASEDIR')
        screenshot_path = os.path.join(basedir, 'ppc/macos10.ppm')

        image = os.getenv('MACOS10_IMAGE')
        if not image:
            self.cancel('No MACOS10_IMAGE environment variable defined')
        drive_path = self.fetch_asset(image)

        self.vm.set_console()
        self.vm.add_args('-M', 'via=pmu')
        self.vm.add_args('-m', '1g')
        self.vm.add_args('-cpu', 'G4')
        self.vm.add_args('-drive', 'file=' + drive_path)
#        self.vm.add_args('-net', 'nic,model=rtl8139')
#        self.vm.add_args('-net', 'user,hostfwd=:127.0.0.1:2223-:22,hostname=qemu')
        self.vm.add_args('-vga', 'std')
        self.vm.add_args('-g', '640x480')
        self.vm.launch()

        # This comes up after 60 seconds on a fast x86 CPU. Give it
        # 180s to be sure.
        time.sleep(180)
        with tempfile.NamedTemporaryFile(suffix='.ppm',
                                         prefix='screendump-', delete=False) as ppmfile:
            self.vm.command('screendump', filename=ppmfile.name)
            if not filecmp.cmp(ppmfile.name, screenshot_path):
                self.fail('Screenshot does not match')
