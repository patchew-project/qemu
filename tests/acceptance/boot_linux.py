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
    """

    timeout = 600

    def test(self):
        self.vm.set_machine(self.params.get('machine', default='pc'))
        self.vm.add_args('-accel', self.params.get('accel', default='kvm'))
        self.vm.add_args('-smp', self.params.get('smp', default='2'))
        self.vm.add_args('-m', self.params.get('memory', default='4096'))

        arch = self.params.get('arch', default=os.uname()[4])
        distro = self.params.get('distro', default='fedora')
        version = self.params.get('version', default='28')
        boot = vmimage.get(distro, arch=arch, version=version,
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
