# Functional tests for the MIPS simulator (MIPSsim machine)
#
# Copyright (c) 2020 Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import logging
import time

from avocado import skipUnless
from avocado_qemu import Test
from avocado_qemu import exec_command_and_wait_for_pattern
from avocado_qemu import interrupt_interactive_console_until_pattern
from avocado_qemu import wait_for_console_pattern

class MipsSimMachine(Test):

    timeout = 30
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '

    @skipUnless(os.getenv('AVOCADO_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
    def test_mipssim_linux_console(self):
        """
        Boots the Linux kernel and checks that the console is operational
        :avocado: tags=arch:mipsel
        :avocado: tags=machine:mipssim
        :avocado: tags=device:mipsnet
        """
        kernel_url = ('https://github.com/philmd/qemu-testing-blob/raw/'
                      '32ea5764e1de8fffa0d59366c44822cd06d7c8e0/'
                      'mips/mipssim/mipsel/vmlinux')
        kernel_hash = '0f9aeca3a2e25b5b0cc4999571f39a7ad58cdc43'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        initrd_url = ('https://github.com/philmd/qemu-testing-blob/raw/'
                      '32ea5764e1de8fffa0d59366c44822cd06d7c8e0/'
                      'mips/mipssim/mipsel/rootfs.cpio')
        initrd_hash = 'b20359bdfae66387e5a17d6692686d59c189417b'
        initrd_path = self.fetch_asset(initrd_url, asset_hash=initrd_hash)

        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path,
                         '-initrd', initrd_path,
                         '-append', self.KERNEL_COMMON_COMMAND_LINE)
        self.vm.launch()

        wait_for_console_pattern(self, 'Welcome to Buildroot')
        interrupt_interactive_console_until_pattern(self,
                                                    interrupt_string='root\r',
                                                    success_message='#')
        pattern = '3 packets transmitted, 3 packets received, 0% packet loss'
        exec_command_and_wait_for_pattern(self, 'ping -c 3 10.0.2.2', pattern)
