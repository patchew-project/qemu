# Xen guest functional tests
#
# Copyright © 2021 Red Hat, Inc.
# Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
#
# Author:
#  David Woodhouse <dwmw2@infradead.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.
import os

from avocado import skipIf
from avocado_qemu import LinuxTest

@skipIf(os.getenv('GITLAB_CI'), 'Running on GitLab')
class XenGuest(LinuxTest):
    """
    :avocado: tags=arch:x86_64
    :avocado: tags=distro:fedora
    :avocado: tags=distro_version:34
    :avocado: tags=machine:q35
    :avocado: tags=accel:kvm
    :avocado: tags=xen_guest
    """

    kernel_path = None
    initrd_path = None
    kernel_params = None

    def set_up_boot(self):
        path = self.download_boot()
        self.vm.add_args('-drive', 'file=%s,if=none,id=drv0' % path)
        self.vm.add_args('-device', 'xen-disk,drive=drv0,vdev=xvda')

    def setUp(self):
        super(XenGuest, self).setUp(None, 'virtio-net-pci')

    def common_vm_setup(self, custom_kernel=None):
        self.require_accelerator("kvm")
        self.vm.add_args("-accel", "kvm,xen-version=0x4000a,kernel-irqchip=split")
        self.vm.add_args("-smp", "4")

        if custom_kernel is None:
            return

        kernel_url = self.distro.pxeboot_url + 'vmlinuz'
        initrd_url = self.distro.pxeboot_url + 'initrd.img'
        self.kernel_path = self.fetch_asset(kernel_url, algorithm='sha256',
                                            asset_hash=self.distro.kernel_hash)
        self.initrd_path = self.fetch_asset(initrd_url, algorithm='sha256',
                                            asset_hash=self.distro.initrd_hash)

    def run_and_check(self):
        if self.kernel_path:
            self.vm.add_args('-kernel', self.kernel_path,
                             '-append', self.kernel_params,
                             '-initrd', self.initrd_path)
        self.launch_and_wait()
        self.ssh_command('cat /proc/cmdline')
        self.ssh_command('dmesg | grep -e "Grant table initialized"')

    def test_xen_guest(self):
        """
        :avocado: tags=xen_guest
        """

        self.common_vm_setup(True)

        self.kernel_params = (self.distro.default_kernel_params +
                              ' xen_emul_unplug=ide-disks')
        self.run_and_check()
        self.ssh_command('grep xen-pirq.*msi /proc/interrupts')

    def test_xen_guest_nomsi(self):
        """
        :avocado: tags=xen_guest_nomsi
        """

        self.common_vm_setup(True)

        self.kernel_params = (self.distro.default_kernel_params +
                              ' xen_emul_unplug=ide-disks pci=nomsi')
        self.run_and_check()
        self.ssh_command('grep xen-pirq.* /proc/interrupts')

    def test_xen_guest_noapic_nomsi(self):
        """
        :avocado: tags=xen_guest_noapic_nomsi
        """

        self.common_vm_setup(True)

        self.kernel_params = (self.distro.default_kernel_params +
                              ' xen_emul_unplug=ide-disks noapic pci=nomsi')
        self.run_and_check()
        self.ssh_command('grep xen-pirq /proc/interrupts')

    def test_xen_guest_vapic(self):
        """
        :avocado: tags=xen_guest_vapic
        """

        self.common_vm_setup(True)
        self.vm.add_args('-cpu', 'host,+xen-vapic')
        self.kernel_params = (self.distro.default_kernel_params +
                              ' xen_emul_unplug=ide-disks')
        self.run_and_check()
        self.ssh_command('grep xen-pirq /proc/interrupts')
        self.ssh_command('grep PCI-MSI /proc/interrupts')

    def test_xen_guest_novector(self):
        """
        :avocado: tags=xen_guest_novector
        """

        self.common_vm_setup(True)
        self.kernel_params = (self.distro.default_kernel_params +
                              ' xen_emul_unplug=ide-disks' +
                              ' xen_no_vector_callback')
        self.run_and_check()
        self.ssh_command('grep xen-platform-pci /proc/interrupts')

    def test_xen_guest_novector_nomsi(self):
        """
        :avocado: tags=xen_guest_novector_nomsi
        """

        self.common_vm_setup(True)

        self.kernel_params = (self.distro.default_kernel_params +
                              ' xen_emul_unplug=ide-disks pci=nomsi' +
                              ' xen_no_vector_callback')
        self.run_and_check()
        self.ssh_command('grep xen-platform-pci /proc/interrupts')

    def test_xen_guest_novector_noapic(self):
        """
        :avocado: tags=xen_guest_novector_noapic
        """

        self.common_vm_setup(True)
        self.kernel_params = (self.distro.default_kernel_params +
                              ' xen_emul_unplug=ide-disks' +
                              ' xen_no_vector_callback noapic')
        self.run_and_check()
        self.ssh_command('grep xen-platform-pci /proc/interrupts')
