# Functional test that boots a Leon3 machine and checks its serial console.
#
# Copyright (c) Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

import os
import logging

from avocado import skipIf
from avocado_qemu import Test


class Leon3Machine(Test):

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

    def exec_command_and_wait_for_pattern(self, command, success_message):
        command += '\n'
        self.vm.console_socket.sendall(command.encode())
        self.wait_for_console_pattern(success_message)

    def test_leon3_helenos_uimage(self):
        """
        :avocado: tags=arch:sparc
        :avocado: tags=machine:leon3
        :avocado: tags=binfmt:uimage
        """
        kernel_url = ('http://www.helenos.org/releases/'
                      'HelenOS-0.6.0-sparc32-leon3.bin')
        kernel_hash = 'a88c9cfdb8430c66650e5290a08765f9bf049a30'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_machine('leon3_generic')
        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path)

        self.vm.launch()

        self.wait_for_console_pattern('Copyright (c) 2001-2014 HelenOS project')
        self.wait_for_console_pattern('Booting the kernel ...')

    def test_leon3_linux_kernel_4_9_busybox(self):
        """
        :avocado: tags=arch:sparc
        :avocado: tags=machine:leon3
        """
        kernel_url = ('https://www.gaisler.com/anonftp/linux/linux-4/images/'
                     'leon-linux-4.9/leon-linux-4.9-1.0/up/image.ram')
        kernel_hash = '289bd1bcca10cda76d0ef2264a8657adc251f5f5'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_machine('leon3_generic')
        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path)

        self.vm.launch()

        self.wait_for_console_pattern('TYPE: Leon3 System-on-a-Chip')

        self.wait_for_console_pattern('Welcome to Buildroot')

        self.wait_for_console_pattern('buildroot login:')
        self.exec_command_and_wait_for_pattern('root', '#')
        uname = 'Linux buildroot 4.9.54-00018-g62dab2c #2 ' \
                'Wed Oct 18 09:45:51 CEST 2017 sparc GNU/Linux'
        self.exec_command_and_wait_for_pattern('uname -a', uname)
