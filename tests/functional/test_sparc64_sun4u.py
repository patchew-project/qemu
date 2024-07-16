#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel and checks the console
#
# Copyright (c) 2020 Red Hat, Inc.
#
# Author:
#  Thomas Huth <thuth@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

import os

from qemu_test import QemuSystemTest
from qemu_test import wait_for_console_pattern
from qemu_test.utils import archive_extract

class Sun4uMachine(QemuSystemTest):
    """Boots the Linux kernel and checks that the console is operational"""

    timeout = 90

    def test_sparc64_sun4u(self):
        self.set_machine('sun4u')
        tar_url = ('https://qemu-advcal.gitlab.io'
                   '/qac-best-of-multiarch/download/day23.tar.xz')
        tar_hash = '142db83cd974ffadc4f75c8a5cad5bcc5722c240'
        file_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        kernel_name = 'day23/vmlinux'
        archive_extract(file_path, self.workdir, kernel_name)
        self.vm.set_console()
        self.vm.add_args('-kernel', os.path.join(self.workdir, kernel_name),
                         '-append', 'printk.time=0')
        self.vm.launch()
        wait_for_console_pattern(self, 'Starting logging: OK')

if __name__ == '__main__':
    QemuSystemTest.main()
