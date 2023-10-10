# Functional test that boots AIX on ppc pseries TCG and KVM
#
# Copyright (c) 2023 IBM Corporation
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os
from avocado.utils import archive
from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern

class pseriesMachine(QemuSystemTest):

    timeout = 600

    def do_test_ppc64_aix_boot(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        """

        image = os.getenv('AIX_IMAGE')
        if not image:
            self.cancel('No AIX_IMAGE environment variable defined')

        _hash = os.getenv('AIX_HASH')
        if _hash:
            aix_disk = self.fetch_asset(image, asset_hash=_hash)
        else:
            aix_disk = self.fetch_asset(image)

        self.vm.set_console()
        self.vm.add_args('-machine', 'ic-mode=xics',
                         '-smp', '16,threads=8,cores=2',
                         '-m', '4g',
#                         '-device', 'spapr-vlan,netdev=net0,mac=52:54:00:49:53:14',
#                         '-netdev', 'tap,id=net0,helper=/usr/libexec/qemu-bridge-helper,br=virbr0',
                         '-device', 'qemu-xhci',
                         '-device', 'virtio-scsi,id=scsi0',
                         '-drive', f'file={aix_disk},if=none,id=drive-scsi0-0-0-0,format=qcow2,cache=none',
                         '-device', 'scsi-hd,bus=scsi0.0,channel=0,scsi-id=0,lun=0,drive=drive-scsi0-0-0-0,id=scsi0-0-0-0,bootindex=1',
                         '-nodefaults')
        self.vm.launch()
        wait_for_console_pattern(self, 'AIX Version 7')

    def test_ppc64_aix_boot_tcg(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        :avocado: tags=accel:tcg
        """
        self.require_accelerator("tcg")
        self.do_test_ppc64_aix_boot()

    def test_ppc64_aix_boot_kvm(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        :avocado: tags=accel:kvm
        """
        self.require_accelerator("kvm")
        self.do_test_ppc64_aix_boot()
