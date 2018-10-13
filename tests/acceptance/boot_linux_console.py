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
import subprocess

from avocado_qemu import Test


class BootLinuxConsole(Test):
    """
    Boots a Linux kernel and checks that the console is operational
    and the kernel command line is properly passed from QEMU to the kernel

    :avocado: enable
    """

    timeout = 60

    def test_x86_64_pc(self):
        if self.arch != 'x86_64':
            self.cancel('Currently specific to the x86_64 target arch')
        kernel_url = ('https://mirrors.kernel.org/fedora/releases/28/'
                      'Everything/x86_64/os/images/pxeboot/vmlinuz')
        kernel_hash = '238e083e114c48200f80d889f7e32eeb2793e02a'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_arch(self.arch)
        self.vm.set_machine()
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

    def test_mips_4kc_malta(self):
        """
        This test requires the dpkg-deb tool (apt/dnf install dpkg) to extract
        the kernel from the Debian package.

        The kernel can be rebuilt using this Debian kernel source [1] and
        following the instructions on [2].

        [1] https://kernel-team.pages.debian.net/kernel-handbook/ch-common-tasks.html#s-common-official
        [2] http://snapshot.debian.org/package/linux-2.6/2.6.32-48/#linux-source-2.6.32_2.6.32-48

        :avocado: tags=arch:mips
        """
        if self.arch != 'mips': # FIXME use 'arch' tag in parent class?
            self.cancel('Currently specific to the %s target arch' % self.arch)

        deb_url = ('http://snapshot.debian.org/archive/debian/20130217T032700Z/'
                   'pool/main/l/linux-2.6/'
                   'linux-image-2.6.32-5-4kc-malta_2.6.32-48_mips.deb')
        deb_hash = 'a8cfc28ad8f45f54811fc6cf74fc43ffcfe0ba04'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash)
        subprocess.check_call(['dpkg-deb', '--extract', deb_path, self.workdir]) # FIXME move to avocado ...
        kernel_path = self.workdir + '/boot/vmlinux-2.6.32-5-4kc-malta'          # FIXME ... and use from assets?

        self.vm.set_arch(self.arch)
        self.vm.set_machine('malta')
        self.vm.set_console("") # XXX this disable isa-serial to use -serial ...
        kernel_command_line = 'console=ttyS0 printk.time=0'
        self.vm.add_args('-m', "64",
                         '-serial', "chardev:console", # XXX ... here.
                         '-kernel', kernel_path,
                         '-append', kernel_command_line)

        # FIXME below to parent class?
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
