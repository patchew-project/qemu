# Functional test that boots a complete Linux system via a cloud image
#
# Copyright (c) 2018-2019 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os

from avocado_qemu import Test

from avocado.utils import cloudinit
from avocado.utils import network
from avocado.utils import vmimage


class BootLinux(Test):
    """
    Boots a Linux system, checking for a successful initialization
    """

    timeout = 600

    def setUp(self):
        super(BootLinux, self).setUp()
        try:
            self.log.info('Downloading and preparing boot image')
            self.boot = vmimage.get(
                'fedora', arch='x86_64', version='29',
                checksum='7109d23215a3911b260a3b9bf3a07aac3436253a',
                cache_dir=self.cache_dirs[0],
                snapshot_dir=self.workdir)
        except:
            self.cancel('Failed to download boot image')

    def test_x86_64_pc(self):
        """
        :avocado: tags=arch:x86_64
        :avocado: tags=machine:pc
        """
        self.vm.set_machine('pc')
        self.vm.add_args('-m', '1024')
        self.vm.add_args('-drive', 'file=%s' % self.boot.path)

        cloudinit_iso = os.path.join(self.workdir, 'cloudinit.iso')
        phone_home_port = network.find_free_port()
        cloudinit.iso(cloudinit_iso, self.name,
                      # QEMU's hard coded usermode router address
                      phone_home_host='10.0.2.2',
                      phone_home_port=phone_home_port)
        self.vm.add_args('-drive', 'file=%s' % cloudinit_iso)

        self.vm.launch()
        self.log.info('VM launched, waiting for boot confirmation from guest')
        cloudinit.wait_for_phone_home(('0.0.0.0', phone_home_port), self.name)
