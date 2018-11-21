# Functional test that boots a complete Linux system via a cloud image
#
# Copyright (c) 2018 Red Hat, Inc.
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

    :avocado: enable
    :avocado: tags=x86_64
    """

    timeout = 600

    def test(self):
        self.vm.add_args('-m', '1024')
        boot = vmimage.get('fedora', arch='x86_64', version='29',
                           cache_dir=self.cache_dirs[0],
                           snapshot_dir=self.workdir)
        self.vm.add_args('-drive', 'file=%s' % boot.path)

        cloudinit_iso = os.path.join(self.workdir, 'cloudinit.iso')
        phone_home_port = network.find_free_port()
        cloudinit.iso(cloudinit_iso, self.name,
                      # QEMU's hard coded usermode router address
                      phone_home_host='10.0.2.2',
                      phone_home_port=phone_home_port)
        self.vm.add_args('-drive', 'file=%s' % cloudinit_iso)

        self.vm.launch()
        cloudinit.wait_for_phone_home(('0.0.0.0', phone_home_port), self.name)
