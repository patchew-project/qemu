# Functional test that boots a Linux kernel and checks the console
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import logging

from avocado_qemu import Test


class BootLinuxConsoleX86_64(Test):
    """
    Boots a x86_64 Linux kernel and checks that the console is operational
    and the kernel command line is properly passed from QEMU to the kernel

    :avocado: enable
    :avocado: tags=endian:little
    :avocado: tags=arch:x86_64
    """

    timeout = 60

    def test(self):
        kernel_url = ('https://mirrors.kernel.org/fedora/releases/28/'
                      'Everything/x86_64/os/images/pxeboot/vmlinuz')
        kernel_hash = '238e083e114c48200f80d889f7e32eeb2793e02a'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_machine('pc')
        self.vm.set_console()
        kernel_command_line = 'console=ttyS0'
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console = self.vm.console_socket.makefile()
        console_logger = logging.getLogger('console')
        while True:
            msg = console.readline()
            console_logger.debug(msg.strip())
            if 'Kernel command line: %s' % kernel_command_line in msg:
                break
            if 'Kernel panic - not syncing' in msg:
                self.fail("Kernel panic reached")


class BootLinuxConsoleMips(Test):
    """
    Boots a mips Linux kernel and checks that the console is operational
    and the kernel command line is properly passed from QEMU to the kernel

    :avocado: enable
    :avocado: tags=endian:big
    :avocado: tags=arch:mips
    :avocado: tags=board:malta
    """

    arch = "mips"
    timeout = 60

    def test(self):
        kernel_url = ('http://people.debian.org/~aurel32/qemu/mips/'
                      'vmlinux-3.2.0-4-4kc-malta')
        kernel_hash = '592e384a4edc16dade52a6cd5c785c637bcbc9ad'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_machine('malta')
        self.vm.set_console("") # FIXME this disable isa-serial to use -serial
        kernel_command_line = 'console=ttyS0 printk.time=0'
        self.vm.add_args('-m', "64",
                         '-serial', "chardev:console", # FIXME ... here.
                         '-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console = self.vm.console_socket.makefile()
        console_logger = logging.getLogger('console')
        while True:
            msg = console.readline()
            console_logger.debug(msg.strip())
            if 'Kernel command line: %s' % kernel_command_line in msg:
                break
            if 'Kernel panic - not syncing' in msg:
                self.fail("Kernel panic reached")
