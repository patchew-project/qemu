# KVM acceptance tests.
#
# Copyright (c) 2019 Red Hat, Inc.
#
# Author:
#  Wainer dos Santos Moschetta <wainersm@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import logging

from avocado_qemu import Test


class Kvm(Test):
    """
    Suite of acceptance tests to check QEMU and KVM integration.
    """

    def test_boot_linux(self):
        """
        Simple Linux boot test with kvm enabled.

        :avocado: tags=arch:x86_64
        :avocado: tags=accel:kvm
        """
        self.vm.add_args('-enable-kvm')
        kernel_url = ('https://download.fedoraproject.org/pub/fedora/linux/'
                      'releases/29/Everything/x86_64/os/images/pxeboot/vmlinuz')
        kernel_hash = '23bebd2680757891cf7adedb033532163a792495'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_machine('pc')
        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path,
                         '-append', 'printk.time=0 console=ttyS0')
        self.vm.launch()

        query = self.vm.command('query-kvm')
        self.assertTrue(query['enabled'])
        self.assertTrue(query['present'])

        console = self.vm.console_socket.makefile()
        console_logger = logging.getLogger('console')
        failure_message = 'Kernel panic - not syncing'
        success_message = 'Booting paravirtualized kernel on KVM'

        while True:
            msg = console.readline().strip()
            if not msg:
                continue
            console_logger.debug(msg)
            if success_message in msg:
                break
            if failure_message in msg:
                fail = 'Failure message found in console: %s' % failure_message
                self.fail(fail)
