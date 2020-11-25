# Functional test that boots an s390x Linux guest with ccw and PCI devices
# attached and checks whether the devices are recognized by Linux
#
# Copyright (c) 2020 Red Hat, Inc.
#
# Author:
#  Cornelia Huck <cohuck@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.


import os

from avocado_qemu import Test
from avocado_qemu import exec_command_and_wait_for_pattern
from avocado_qemu import wait_for_console_pattern

class CheckS390xDevices(Test):
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing',
                                 vm=vm)

    timeout = 60

    def test(self):

        """
        :avocado: tags=arch:s390x
        :avocado: tags=machine:s390-ccw-virtio
        """

        # XXX: switch to https when debian fixes their certificate
        kernel_url = ('http://archive.debian.org/debian/dists/jessie/main'
                      '/installer-s390x/current/images/generic/kernel.debian')
        kernel_hash = '5af1aa839754f4d8817fb5878b4d55dfc887f45d'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        initrd_url = ('http://archive.debian.org/debian/dists/jessie/main'
                      '/installer-s390x/current/images/generic/initrd.debian')
        initrd_hash = '99252b28306184b876f979585e2d4bfe96b27464'
        initrd_path = self.fetch_asset(initrd_url, asset_hash=initrd_hash)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                              'console=sclp0 root=/dev/ram0 BOOT_DEBUG=3')
        self.vm.add_args('-nographic',
                         '-kernel', kernel_path,
                         '-initrd', initrd_path,
                         '-append', kernel_command_line,
                         '-device', 'virtio-net-ccw,devno=fe.1.1111',
                         '-device', 'virtio-net-pci')
        self.vm.launch()

        shell_ready = "sh: can't access tty; job control turned off"
        self.wait_for_console_pattern(shell_ready)
        # first debug shell is too early, we need to wait for device detection
        exec_command_and_wait_for_pattern(self, 'exit', shell_ready)

        ccw_bus_id="0.1.1111"
        pci_bus_id="0000:00:00.0"
        exec_command_and_wait_for_pattern(self, 'ls /sys/bus/ccw/devices/',
                                          ccw_bus_id)
        exec_command_and_wait_for_pattern(self, 'ls /sys/bus/pci/devices/',
                                          pci_bus_id)
