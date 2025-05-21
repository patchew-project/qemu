#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The test hotplugs a PCI device and checks it on a Linux guest.
#
# Copyright (c) 2025 Linaro Ltd.
#
# Author:
#  Gustavo Romero <gustavo.romero@linaro.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import LinuxKernelTest, Asset, exec_command_and_wait_for_pattern
from qemu_test import BUILD_DIR

class HotplugPCI(LinuxKernelTest):

    ASSET_KERNEL = Asset(
        ('https://ftp.debian.org/debian/dists/stable/main/installer-arm64/'
         'current/images/netboot/debian-installer/arm64/linux'),
        '3821d4db56d42c6a4eac62f31846e35465940afd87746b4cfcdf5c9eca3117b2')

    ASSET_INITRD = Asset(
        ('https://ftp.debian.org/debian/dists/stable/main/installer-arm64/'
         'current/images/netboot/debian-installer/arm64/initrd.gz'),
        '2583ec22b45265ad69e82f198674f53d4cd85be124fe012eedc2fd91156bc4b4')

    def test_hotplug_pci(self):

        self.set_machine('virt')
        self.vm.add_args('-m', '512M')
        self.vm.add_args('-cpu', 'cortex-a57')
        self.vm.add_args('-append',
                         'console=ttyAMA0,115200 init=/bin/sh')
        self.vm.add_args('-device',
                         'pcie-root-port,bus=pcie.0,chassis=1,slot=1,id=pcie.1')
        self.vm.add_args('-bios', self.build_file('pc-bios',
                                                  'edk2-aarch64-code.fd'))

        # BusyBox prompt
        prompt = "~ #"
        self.launch_kernel(self.ASSET_KERNEL.fetch(),
                           self.ASSET_INITRD.fetch(),
                           wait_for=prompt)

        # Check for initial state: 2 network adapters, lo and enp0s1.
        exec_command_and_wait_for_pattern(self,
                                          'ls -l /sys/class/net | wc -l',
                                          '2')

        # Hotplug one network adapter to the root port, i.e. pcie.1 bus.
        self.vm.cmd('device_add',
                    driver='virtio-net-pci',
                    bus='pcie.1',
                    addr=0,
                    id='na')
        # Wait for the kernel to recognize the new device.
        self.wait_for_console_pattern('virtio-pci')
        self.wait_for_console_pattern('virtio_net')

        # Check if there is a new network adapter.
        exec_command_and_wait_for_pattern(self,
                                          'ls -l /sys/class/net | wc -l',
                                          '3')

        self.vm.cmd('device_del', id='na')
        exec_command_and_wait_for_pattern(self,
                                          'ls -l /sys/class/net | wc -l',
                                          '2')

if __name__ == '__main__':
    LinuxKernelTest.main()
