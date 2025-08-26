#!/usr/bin/env python3
#
# Copyright (c) 2025 Nutanix, Inc.
#
# Author:
#  Mark Cave-Ayland <mark.caveayland@nutanix.com>
#  John Levon <john.levon@nutanix.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
"""
Check basic vfio-user-pci client functionality. The test starts two VMs:

    - the server VM runs the libvfio-user "gpio" example server inside it,
      piping vfio-user traffic between a local UNIX socket and a virtio-serial
      port. On the host, the virtio-serial port is backed by a local socket.

    - the client VM loads the gpio-pci-idio-16 kernel module, with the
      vfio-user client connecting to the above local UNIX socket.

This way, we don't depend on trying to run a vfio-user server on the host
itself.

Once both VMs are running, we run some basic configuration on the gpio device
and verify that the server is logging the expected out. As this is consistent
given the same VM images, we just do a simple direct comparison.
"""

import difflib
import logging
import os
import re
import select
import shutil
import socket
import subprocess
import time

from qemu_test import Asset
from qemu_test import QemuSystemTest
from qemu_test import exec_command
from qemu_test import exec_command_and_wait_for_pattern
from qemu_test import wait_for_console_pattern

EXPECTED_SERVER_OUT=\
"""gpio: adding DMA region [0, 0xc0000) offset=0 flags=0x3
gpio: adding DMA region [0xc0000, 0xe0000) offset=0 flags=0x1
gpio: adding DMA region [0xe0000, 0x100000) offset=0 flags=0x1
gpio: adding DMA region [0x100000, 0x8000000) offset=0 flags=0x3
gpio: adding DMA region [0xfffc0000, 0x100000000) offset=0 flags=0x1
gpio: devinfo flags 0x3, num_regions 9, num_irqs 5
gpio: region_info[0] offset 0 flags 0 size 0 argsz 32
gpio: region_info[1] offset 0 flags 0 size 0 argsz 32
gpio: region_info[2] offset 0 flags 0x3 size 256 argsz 32
gpio: region_info[3] offset 0 flags 0 size 0 argsz 32
gpio: region_info[4] offset 0 flags 0 size 0 argsz 32
gpio: region_info[5] offset 0 flags 0 size 0 argsz 32
gpio: region_info[7] offset 0 flags 0x3 size 256 argsz 32
gpio: region7: read 256 bytes at 0
gpio: region7: read 0 from (0x30:4)
gpio: write mask to EROM ignored
gpio: region7: wrote 0xfffff800 to (0x30:4)
gpio: region7: read 0 from (0x30:4)
gpio: cleared EROM
gpio: region7: wrote 0 to (0x30:4)
gpio: region7: read 0x1 from (0x18:4)
gpio: region7: read 0x1 from (0x3d:1)
gpio: region7: read 0x1 from (0x3d:1)
gpio: disabling IRQ type INTx range [0, 1)
gpio: disabling IRQ type ERR range [0, 1)
gpio: disabling IRQ type INTx range [0, 1)
gpio: region7: read 0 from (0x4:2)
gpio: region7: wrote 0 to (0x4:2)
gpio: device reset by client
gpio: region7: read 0x1 from (0x3d:1)
gpio: disabling IRQ type INTx range [0, 1)
gpio: region7: wrote 0 to (0x10:4)
gpio: region7: wrote 0 to (0x14:4)
gpio: BAR2 addr 0x0
gpio: region7: wrote 0 to (0x18:4)
gpio: region7: wrote 0 to (0x1c:4)
gpio: region7: wrote 0 to (0x20:4)
gpio: region7: wrote 0 to (0x24:4)
gpio: removing DMA region [0, 0xc0000) flags=0
gpio: removing DMA region [0xc0000, 0xe0000) flags=0
gpio: removing DMA region [0xe0000, 0x100000) flags=0
gpio: removing DMA region [0x100000, 0x8000000) flags=0
gpio: adding DMA region [0, 0xd0000) offset=0 flags=0x3
gpio: adding DMA region [0xd0000, 0xe0000) offset=0 flags=0x1
gpio: adding DMA region [0xe0000, 0xf0000) offset=0 flags=0x1
gpio: adding DMA region [0xf0000, 0x8000000) offset=0 flags=0x3
gpio: removing DMA region [0, 0xd0000) flags=0
gpio: removing DMA region [0xd0000, 0xe0000) flags=0
gpio: removing DMA region [0xe0000, 0xf0000) flags=0
gpio: removing DMA region [0xf0000, 0x8000000) flags=0
gpio: adding DMA region [0, 0x8000000) offset=0 flags=0x3
gpio: region7: read 0x494f from (0:2)
gpio: region7: read 0 from (0xe:1)
gpio: region7: read 0x494f from (0:2)
gpio: region7: read 0 from (0xe:1)
gpio: region7: read 0x494f from (0:2)
gpio: region7: read 0xdc8494f from (0:4)
gpio: region7: read 0 from (0x8:4)
gpio: region7: read 0 from (0xe:1)
gpio: region7: read 0 from (0xe:1)
gpio: region7: wrote 0xffffffff to (0x10:4)
gpio: region7: wrote 0 to (0x10:4)
gpio: region7: wrote 0xffffffff to (0x14:4)
gpio: region7: wrote 0 to (0x14:4)
gpio: BAR2 addr 0xffffffff
gpio: region7: wrote 0xffffffff to (0x18:4)
gpio: BAR2 addr 0x1
gpio: region7: wrote 0x1 to (0x18:4)
gpio: region7: wrote 0xffffffff to (0x1c:4)
gpio: region7: wrote 0 to (0x1c:4)
gpio: region7: wrote 0xffffffff to (0x20:4)
gpio: region7: wrote 0 to (0x20:4)
gpio: region7: wrote 0xffffffff to (0x24:4)
gpio: region7: wrote 0 to (0x24:4)
gpio: write mask to EROM ignored
gpio: region7: wrote 0xfffff800 to (0x30:4)
gpio: cleared EROM
gpio: region7: wrote 0 to (0x30:4)
gpio: BAR2 addr 0xc000
gpio: region7: wrote 0xc000 to (0x18:4)
gpio: region7: read 0x1 from (0x3d:1)
gpio: ILINE=b
gpio: region7: wrote 0xb to (0x3c:1)
gpio: region7: read 0 from (0x4:2)
gpio: I/O space enabled
gpio: memory space enabled
gpio: SERR# enabled
gpio: region7: wrote 0x103 to (0x4:2)
gpio: region7: read 0x1 from (0x3d:1)
gpio: region7: read 0xb from (0x3c:1)
gpio: adding DMA region [0xfeb80000, 0xfebc0000) offset=0 flags=0x1
gpio: removing DMA region [0xfeb80000, 0xfebc0000) flags=0
gpio: EROM disable ignored
gpio: region7: wrote 0xfffffffe to (0x30:4)
gpio: cleared EROM
gpio: region7: wrote 0 to (0x30:4)
gpio: removing DMA region [0, 0x8000000) flags=0
gpio: adding DMA region [0, 0xc0000) offset=0 flags=0x3
gpio: adding DMA region [0xc0000, 0xc1000) offset=0 flags=0x1
gpio: adding DMA region [0xc1000, 0xc4000) offset=0 flags=0x3
gpio: adding DMA region [0xc4000, 0xd0000) offset=0 flags=0x1
gpio: adding DMA region [0xd0000, 0xf0000) offset=0 flags=0x3
gpio: adding DMA region [0xf0000, 0x100000) offset=0 flags=0x1
gpio: adding DMA region [0x100000, 0x8000000) offset=0 flags=0x3
gpio: removing DMA region [0xc4000, 0xd0000) flags=0
gpio: removing DMA region [0xd0000, 0xf0000) flags=0
gpio: adding DMA region [0xc4000, 0xe8000) offset=0 flags=0x1
gpio: adding DMA region [0xe8000, 0xf0000) offset=0 flags=0x3
gpio: region7: read 0x494f from (0:2)
gpio: region7: read 0xdc8 from (0x2:2)
gpio: region7: read 0 from (0xe:1)
gpio: region7: read 0xdc8494f from (0:4)
gpio: region7: read 0 from (0xe:1)
gpio: region7: read 0 from (0x6:2)
gpio: region7: read 0 from (0x8:4)
gpio: region7: read 0 from (0x6:2)
gpio: region7: read 0x103 from (0x4:2)
gpio: INTx emulation disabled
gpio: region7: wrote 0x503 to (0x4:2)
gpio: region7: read 0x503 from (0x4:2)
gpio: INTx emulation enabled
gpio: region7: wrote 0x103 to (0x4:2)
gpio: region7: read 0x1 from (0x3d:1)
gpio: region7: read 0xb from (0x3c:1)
gpio: region7: read 0x103 from (0x4:2)
gpio: I/O space disabled
gpio: memory space disabled
gpio: region7: wrote 0x100 to (0x4:2)
gpio: region7: wrote 0xffffffff to (0x10:4)
gpio: region7: wrote 0 to (0x10:4)
gpio: I/O space enabled
gpio: memory space enabled
gpio: region7: wrote 0x103 to (0x4:2)
gpio: region7: read 0x103 from (0x4:2)
gpio: I/O space disabled
gpio: memory space disabled
gpio: region7: wrote 0x100 to (0x4:2)
gpio: region7: wrote 0xffffffff to (0x14:4)
gpio: region7: wrote 0 to (0x14:4)
gpio: I/O space enabled
gpio: memory space enabled
gpio: region7: wrote 0x103 to (0x4:2)
gpio: region7: read 0x103 from (0x4:2)
gpio: I/O space disabled
gpio: memory space disabled
gpio: region7: wrote 0x100 to (0x4:2)
gpio: BAR2 addr 0xffffffff
gpio: region7: wrote 0xffffffff to (0x18:4)
gpio: BAR2 addr 0xc001
gpio: region7: wrote 0xc001 to (0x18:4)
gpio: I/O space enabled
gpio: memory space enabled
gpio: region7: wrote 0x103 to (0x4:2)
gpio: region7: read 0x103 from (0x4:2)
gpio: I/O space disabled
gpio: memory space disabled
gpio: region7: wrote 0x100 to (0x4:2)
gpio: region7: wrote 0xffffffff to (0x1c:4)
gpio: region7: wrote 0 to (0x1c:4)
gpio: I/O space enabled
gpio: memory space enabled
gpio: region7: wrote 0x103 to (0x4:2)
gpio: region7: read 0x103 from (0x4:2)
gpio: I/O space disabled
gpio: memory space disabled
gpio: region7: wrote 0x100 to (0x4:2)
gpio: region7: wrote 0xffffffff to (0x20:4)
gpio: region7: wrote 0 to (0x20:4)
gpio: I/O space enabled
gpio: memory space enabled
gpio: region7: wrote 0x103 to (0x4:2)
gpio: region7: read 0x103 from (0x4:2)
gpio: I/O space disabled
gpio: memory space disabled
gpio: region7: wrote 0x100 to (0x4:2)
gpio: region7: wrote 0xffffffff to (0x24:4)
gpio: region7: wrote 0 to (0x24:4)
gpio: I/O space enabled
gpio: memory space enabled
gpio: region7: wrote 0x103 to (0x4:2)
gpio: region7: read 0x103 from (0x4:2)
gpio: I/O space disabled
gpio: memory space disabled
gpio: region7: wrote 0x100 to (0x4:2)
gpio: write mask to EROM ignored
gpio: region7: wrote 0xfffff800 to (0x30:4)
gpio: cleared EROM
gpio: region7: wrote 0 to (0x30:4)
gpio: I/O space enabled
gpio: memory space enabled
gpio: region7: wrote 0x103 to (0x4:2)
gpio: region7: read 0 from (0x2c:2)
gpio: region7: read 0 from (0x2e:2)
gpio: region7: read 0 from (0x6:2)
gpio: region7: read 0 from (0x6:2)
gpio: region7: read 0 from (0x6:2)
gpio: region7: read 0 from (0x6:2)
gpio: region7: read 0 from (0x6:2)
gpio: region7: read 0 from (0x6:2)
gpio: region7: read 0 from (0x6:2)
gpio: region7: read 0 from (0x6:2)
gpio: region7: read 0x103 from (0x4:2)
gpio: region7: read 0x103 from (0x4:2)
gpio: region7: read 0 from (0xc:1)
gpio: region7: read 0x103 from (0x4:2)
gpio: region7: read 0x1 from (0x3d:1)
gpio: region7: read 0x103 from (0x4:2)
gpio: region2: wrote 0 to (0x3:1)
gpio: region2: wrote 0 to (0x2:1)
gpio: region2: wrote 0 to (0x1:1)"""


class VfioUserClient(QemuSystemTest):

    ASSET_REPO = 'https://github.com/mcayland-ntx/libvfio-user-test'

    ASSET_KERNEL = Asset(
        f'{ASSET_REPO}/raw/refs/heads/main/images/bzImage',
        '40292fa6ce95d516e26bccf5974e138d0db65a6de0bc540cabae060fe9dea605'
    )

    ASSET_ROOTFS = Asset(
        f'{ASSET_REPO}/raw/refs/heads/main/images/rootfs.ext2',
        'e1e3abae8aebb8e6e77f08b1c531caeacf46250c94c815655c6bbea59fc3d1c1'
    )


    def prepare_images(self):
        """Set up the images for the VMs."""
        self.kernel_path = self.ASSET_KERNEL.fetch()
        rootfs_path = self.ASSET_ROOTFS.fetch()

        self.server_rootfs_path = self.scratch_file('server.ext2')
        shutil.copy(rootfs_path, self.server_rootfs_path)
        os.chmod(self.server_rootfs_path, 0o600)
        self.client_rootfs_path = self.scratch_file('client.ext2')
        shutil.copy(rootfs_path, self.client_rootfs_path)
        os.chmod(self.client_rootfs_path, 0o600)

    def configure_server_vm_args(self, server_vm, sock_path):
        """
        Configuration for the server VM. Set up virtio-serial device backed by
        the given socket path.
        """
        server_vm.add_args('-kernel', self.kernel_path)
        server_vm.add_args('-append', 'console=ttyS0 root=/dev/sda')
        server_vm.add_args('-drive',
            f"file={self.server_rootfs_path},if=ide,format=raw,id=drv0")
        server_vm.add_args('-snapshot')
        server_vm.add_args('-chardev',
            f"socket,id=sock0,path={sock_path},telnet=off,server=on,wait=off")
        server_vm.add_args('-device', 'virtio-serial')
        server_vm.add_args('-device',
            'virtserialport,chardev=sock0,name=org.fedoraproject.port.0')

    def configure_client_vm_args(self, client_vm, sock_path):
        """
        Configuration for the client VM. Point the vfio-user-pci device to the
        socket path configured above.
        """

        client_vm.add_args('-kernel', self.kernel_path)
        client_vm.add_args('-append', 'console=ttyS0 root=/dev/sda')
        client_vm.add_args('-drive',
            f'file={self.client_rootfs_path},if=ide,format=raw,id=drv0')
        client_vm.add_args('-device',
            '{"driver":"vfio-user-pci",' +
            '"socket":{"path": "%s", "type": "unix"}}' % sock_path)

    def setup_vfio_user_pci_server(self, server_vm):
        """
        Start the libvfio-user server within the server VM, and arrange
        for data to shuttle between its socket and the virtio serial port.
        """
        wait_for_console_pattern(self, 'login:', None, server_vm)
        exec_command_and_wait_for_pattern(self, 'root', '#', None, server_vm)

        exec_command_and_wait_for_pattern(self,
            'gpio-pci-idio-16 -v /tmp/vfio-user.sock >/var/tmp/gpio.out 2>&1 &',
            '#', None, server_vm)
        # wait for libvfio-user to initialize properly
        exec_command_and_wait_for_pattern(self, 'sleep 5', '#', None, server_vm)
        exec_command_and_wait_for_pattern(self,
            'socat UNIX-CONNECT:/tmp/vfio-user.sock /dev/vport0p1,ignoreeof ' +
            ' &', '#', None, server_vm)

    def test_vfio_user_pci(self):
        self.prepare_images()
        self.set_machine('pc')
        self.require_device('virtio-serial')
        self.require_device('vfio-user-pci')

        sock_dir = self.socket_dir()
        socket_path = sock_dir.name + '/vfio-user.sock'
        socket_path = '/tmp/vfio-user.sock'

        server_vm = self.get_vm(name='server')
        server_vm.set_console()
        self.configure_server_vm_args(server_vm, socket_path)

        server_vm.launch()

        self.log.debug('starting libvfio-user server')

        self.setup_vfio_user_pci_server(server_vm)

        client_vm = self.get_vm(name="client")
        client_vm.set_console()
        self.configure_client_vm_args(client_vm, socket_path)

        try:
            client_vm.launch()
        except:
            self.log.error('client VM failed to start, dumping server logs')
            exec_command_and_wait_for_pattern(self, 'cat /var/tmp/gpio.out',
                '#', None, server_vm)
            raise

        self.log.debug('waiting for client VM boot')

        wait_for_console_pattern(self, 'login:', None, client_vm)
        exec_command_and_wait_for_pattern(self, 'root', '#', None, client_vm)

        #
        # Here, we'd like to actually interact with the gpio device a little
        # more as described at:
        #
        # https://github.com/nutanix/libvfio-user/blob/master/docs/qemu.md
        #
        # Unfortunately, the buildroot Linux kernel has some undiagnosed issue
        # so we don't get /sys/class/gpio. Nonetheless just the basic
        # initialization and setup is enough for basic testing of vfio-user.
        #

        self.log.debug('collecting libvfio-user server output')

        out = exec_command_and_wait_for_pattern(self,
            'cat /var/tmp/gpio.out',
            'gpio: region2: wrote 0 to (0x1:1)',
            None, server_vm)

        pattern = re.compile(r'^gpio:')

        gpio_server_out = [s for s in out.decode().splitlines()
                                   if pattern.search(s)]

        expected_server_out = EXPECTED_SERVER_OUT.splitlines()

        if gpio_server_out != expected_server_out:
            self.log.error('Server logs did not match:')
            print("orig")
            print(gpio_server_out)
            print("second")
            print(expected_server_out)
            for line in difflib.unified_diff(expected_server_out,
                                             gpio_server_out):
                self.log.error(line)

            self.assertTrue(gpio_server_out == expected_server_out)


if __name__ == '__main__':
    QemuSystemTest.main()
