# x2APIC with TCG accelerator tests
# Based on intel_iommu.py, INTEL_IOMMU Functional tests
#
# Copyright (c) Bui Quang Minh <minhquangbui99@gmail.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.
import os

from avocado import skipIf
from avocado_qemu import LinuxTest

@skipIf(os.getenv('GITLAB_CI'), 'Running on GitLab')
class TCGx2APIC(LinuxTest):
    """
    :avocado: tags=arch:x86_64
    :avocado: tags=distro:fedora
    :avocado: tags=distro_version:31
    :avocado: tags=machine:q35
    :avocado: tags=accel:tcg
    :avocado: tags=x2apic
    """

    IOMMU_ADDON = ',iommu_platform=on,disable-modern=off,disable-legacy=on'
    kernel_path = None
    initrd_path = None
    kernel_params = None

    def set_up_boot(self):
        path = self.download_boot()
        self.vm.add_args('-device', 'virtio-blk-pci,bus=pcie.0,scsi=off,' +
                         'drive=drv0,id=virtio-disk0,bootindex=1,'
                         'werror=stop,rerror=stop' + self.IOMMU_ADDON)
        self.vm.add_args('-device', 'virtio-gpu-pci' + self.IOMMU_ADDON)
        self.vm.add_args('-drive',
                         'file=%s,if=none,cache=writethrough,id=drv0' % path)

    def setUp(self):
        super(TCGx2APIC, self).setUp(None, 'virtio-net-pci' + self.IOMMU_ADDON)

    def add_common_args(self):
        self.vm.add_args('-device', 'virtio-rng-pci,rng=rng0')
        self.vm.add_args('-object',
                         'rng-random,id=rng0,filename=/dev/urandom')

    def common_vm_setup(self, custom_kernel=None):
        self.require_accelerator('tcg')
        self.add_common_args()
        self.vm.add_args('-accel', 'tcg')
        self.vm.add_args('-device', 'intel-iommu,intremap=on,eim=on')
        self.vm.add_args('-cpu', 'qemu64,+x2apic')

        if custom_kernel is None:
            return

        kernel_url = self.distro.pxeboot_url + 'vmlinuz'
        initrd_url = self.distro.pxeboot_url + 'initrd.img'
        self.kernel_path = self.fetch_asset(kernel_url)
        self.initrd_path = self.fetch_asset(initrd_url)

    def run_and_check(self):
        if self.kernel_path:
            self.vm.add_args('-kernel', self.kernel_path,
                             '-append', self.kernel_params,
                             '-initrd', self.initrd_path)
        self.launch_and_wait()
        self.ssh_command('cat /proc/cmdline')
        self.ssh_command('dmesg | grep "x2apic enabled"')

    def test_physical_x2apic(self):
        """
        :avocado: tags=physical_x2apic
        """

        self.common_vm_setup(True)

        self.kernel_params = (self.distro.default_kernel_params +
                              ' quiet intel_iommu=on x2apic_phys')
        self.run_and_check()
        self.ssh_command('dmesg | grep "Switched APIC routing to physical x2apic"')

    def test_cluster_x2apic(self):
        """
        :avocado: tags=cluster_x2apic
        """

        self.common_vm_setup(True)
        self.kernel_params = (self.distro.default_kernel_params +
                              ' quiet intel_iommu=on')
        self.run_and_check()
        self.ssh_command('dmesg | grep "Switched APIC routing to cluster x2apic"')
