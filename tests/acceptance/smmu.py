# SMMUv3 Functional tests
#
# Copyright (c) 2021 Red Hat, Inc.
#
# Author:
#  Eric Auger <eric.auger@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os

from avocado_qemu import LinuxTest, BUILD_DIR
from avocado.utils import ssh

class SMMU(LinuxTest):

    KERNEL_COMMON_PARAMS = ("root=UUID=b6950a44-9f3c-4076-a9c2-355e8475b0a7 ro "
                            "earlyprintk=pl011,0x9000000 ignore_loglevel "
                            "no_timer_check printk.time=1 rd_NO_PLYMOUTH "
                            "console=ttyAMA0 ")
    IOMMU_ADDON = ',iommu_platform=on,disable-modern=off,disable-legacy=on'
    IMAGE = ("https://archives.fedoraproject.org/pub/archive/fedora/"
             "linux/releases/31/Everything/aarch64/os/images/pxeboot/")
    kernel_path = None
    initrd_path = None
    kernel_params = None

    def set_up_boot(self):
        path = self.download_boot()
        self.vm.add_args('-device', 'virtio-blk-pci,bus=pcie.0,scsi=off,' +
                         'drive=drv0,id=virtio-disk0,bootindex=1,'
                         'werror=stop,rerror=stop' + self.IOMMU_ADDON)
        self.vm.add_args('-drive',
                         'file=%s,if=none,cache=writethrough,id=drv0' % path)

    def setUp(self):
        super(SMMU, self).setUp(None, 'virtio-net-pci' + self.IOMMU_ADDON)

    def add_common_args(self):
        self.vm.add_args("-machine", "virt")
        self.vm.add_args('-bios', os.path.join(BUILD_DIR, 'pc-bios',
                                      'edk2-aarch64-code.fd'))
        self.vm.add_args('-device', 'virtio-rng-pci,rng=rng0')
        self.vm.add_args('-object',
                         'rng-random,id=rng0,filename=/dev/urandom')

    def common_vm_setup(self, custom_kernel=None):
        self.require_accelerator("kvm")
        self.add_common_args()
        self.vm.add_args("-accel", "kvm")
        self.vm.add_args("-cpu", "host")
        self.vm.add_args("-machine", "iommu=smmuv3")

        if custom_kernel is None:
            return

        kernel_url = self.IMAGE + 'vmlinuz'
        initrd_url = self.IMAGE + 'initrd.img'
        self.kernel_path = self.fetch_asset(kernel_url)
        self.initrd_path = self.fetch_asset(initrd_url)

    def run_and_check(self):
        if self.kernel_path:
            self.vm.add_args('-kernel', self.kernel_path,
                             '-append', self.kernel_params,
                             '-initrd', self.initrd_path)
        self.launch_and_wait()
        self.ssh_command('cat /proc/cmdline')
        self.ssh_command('dnf -y install numactl-devel')

    def test_smmu(self):
        """
        :avocado: tags=accel:kvm
        :avocado: tags=cpu:host
        :avocado: tags=smmu
        """

        self.common_vm_setup()
        self.run_and_check()

    def test_smmu_passthrough(self):
        """
        :avocado: tags=accel:kvm
        :avocado: tags=cpu:host
        :avocado: tags=smmu
        """
        self.common_vm_setup(True)

        self.kernel_params = self.KERNEL_COMMON_PARAMS + 'iommu.passthrough=on'

        self.run_and_check()

    def test_smmu_nostrict(self):
        """
        :avocado: tags=accel:kvm
        :avocado: tags=cpu:host
        :avocado: tags=smmu
        """
        self.common_vm_setup(True)

        self.kernel_params = self.KERNEL_COMMON_PARAMS + 'iommu.strict=0'

        self.run_and_check()
