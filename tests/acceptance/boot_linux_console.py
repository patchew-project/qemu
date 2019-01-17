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
import os

from avocado_qemu import Test
from avocado.utils import process
from avocado.utils import archive


class BootLinuxConsole(Test):
    """
    Boots a Linux kernel and checks that the console is operational and the
    kernel command line is properly passed from QEMU to the kernel

    :avocado: enable
    """

    timeout = 60

    def wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing'):
        """
        Waits for messages to appear on the console, while logging the content

        :param success_message: if this message appears, test succeeds
        :param failure_message: if this message appears, test fails
        """
        console = self.vm.console_socket.makefile()
        console_logger = logging.getLogger('console')
        while True:
            msg = console.readline()
            console_logger.debug(msg.strip())
            if success_message in msg:
                break
            if failure_message in msg:
                fail = 'Failure message found in console: %s' % failure_message
                self.fail(fail)

    def test_x86_64_pc(self):
        """
        :avocado: tags=arch:x86_64
        :avocado: tags=machine:pc
        """
        kernel_url = ('https://mirrors.kernel.org/fedora/releases/29/'
                      'Everything/x86_64/os/images/pxeboot/vmlinuz')
        kernel_hash = '23bebd2680757891cf7adedb033532163a792495'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_machine('pc')
        self.vm.set_console()
        kernel_command_line = 'console=ttyS0'
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)

    def test_mips_malta(self):
        """
        :avocado: tags=arch:mips
        :avocado: tags=machine:malta
        :avocado: tags=endian:big
        """
        kernel_url = ('http://people.debian.org/~aurel32/qemu/mips/'
                      'vmlinux-3.2.0-4-4kc-malta')
        kernel_hash = '592e384a4edc16dade52a6cd5c785c637bcbc9ad'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_machine('malta')
        self.vm.set_console()
        kernel_command_line = 'console=ttyS0 printk.time=0'
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)

    def test_mips64el_malta(self):
        """
        This test requires the ar tool to extract "data.tar.gz" from
        the Debian package.

        The kernel can be rebuilt using this Debian kernel source [1] and
        following the instructions on [2].

        [1] https://kernel-team.pages.debian.net/kernel-handbook/ch-common-tasks.html#s-common-official
        [2] http://snapshot.debian.org/package/linux-2.6/2.6.32-48/#linux-source-2.6.32_2.6.32-48

        :avocado: tags=arch:mips64el
        :avocado: tags=machine:malta
        """
        deb_url = ('http://snapshot.debian.org/archive/debian/20130217T032700Z/'
                   'pool/main/l/linux-2.6/'
                   'linux-image-2.6.32-5-5kc-malta_2.6.32-48_mipsel.deb')
        deb_hash = '1aaec92083bf22fda31e0d27fa8d9a388e5fc3d5'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash)

        cwd = os.getcwd()
        os.chdir(self.workdir)
        process.run("ar x %s data.tar.gz" % deb_path)
        archive.extract("data.tar.gz", self.workdir)
        os.chdir(cwd)
        kernel_path = self.workdir + '/boot/vmlinux-2.6.32-5-5kc-malta'

        self.vm.set_machine('malta')
        self.vm.set_console()
        kernel_command_line = 'console=ttyS0 printk.time=0'
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)

    def test_ppc64_pseries(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        """
        kernel_url = ('http://mirrors.rit.edu/fedora/fedora-secondary/'
                      'releases/29/Everything/ppc64le/os/ppc/ppc64/vmlinuz')
        kernel_hash = '3fe04abfc852b66653b8c3c897a59a689270bc77'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_machine('pseries')
        self.vm.set_console()
        kernel_command_line = 'console=hvc0'
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)
