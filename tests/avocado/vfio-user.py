# vfio-user protocol sanity test
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.


import os
import socket
import uuid

from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern
from avocado_qemu import exec_command
from avocado_qemu import exec_command_and_wait_for_pattern

from avocado.utils import network
from avocado.utils import wait

class VfioUser(QemuSystemTest):
    """
    :avocado: tags=vfiouser
    """
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '
    timeout = 20

    def _get_free_port(self):
        port = network.find_free_port()
        if port is None:
            self.cancel('Failed to find a free port')
        return port

    def validate_vm_launch(self, vm):
        wait_for_console_pattern(self, 'as init process',
                                 'Kernel panic - not syncing', vm=vm)
        exec_command(self, 'mount -t sysfs sysfs /sys', vm=vm)
        exec_command_and_wait_for_pattern(self,
                                          'cat /sys/bus/pci/devices/*/uevent',
                                          'PCI_ID=1000:0060', vm=vm)

    def launch_server_startup(self, socket, *opts):
        server_vm = self.get_vm()
        server_vm.add_args('-machine', 'x-remote,vfio-user=on')
        server_vm.add_args('-nodefaults')
        server_vm.add_args('-device', 'megasas,id=sas1')
        server_vm.add_args('-object', 'x-vfio-user-server,id=vfioobj1,'
                           'type=unix,path='+socket+',device=sas1')
        for opt in opts:
            server_vm.add_args(opt)
        server_vm.launch()
        return server_vm

    def launch_server_hotplug(self, socket):
        server_vm = self.get_vm()
        server_vm.add_args('-machine', 'x-remote,vfio-user=on')
        server_vm.add_args('-nodefaults')
        server_vm.launch()
        server_vm.qmp('device_add', args_dict=None, conv_keys=None,
                      driver='megasas', id='sas1')
        obj_add_opts = {'qom-type': 'x-vfio-user-server',
                        'id': 'vfioobj', 'device': 'sas1',
                        'socket': {'type': 'unix', 'path': socket}}
        server_vm.qmp('object-add', args_dict=obj_add_opts)
        return server_vm

    def launch_client(self, kernel_path, initrd_path, kernel_command_line,
                      machine_type, socket, *opts):
        client_vm = self.get_vm()
        client_vm.set_console()
        client_vm.add_args('-machine', machine_type)
        client_vm.add_args('-accel', 'kvm')
        client_vm.add_args('-cpu', 'host')
        client_vm.add_args('-object',
                           'memory-backend-memfd,id=sysmem-file,size=2G')
        client_vm.add_args('--numa', 'node,memdev=sysmem-file')
        client_vm.add_args('-m', '2048')
        client_vm.add_args('-kernel', kernel_path,
                           '-initrd', initrd_path,
                           '-append', kernel_command_line)
        client_vm.add_args('-device',
                           'vfio-user-pci,socket='+socket)
        for opt in opts:
            client_vm.add_args(opt)
        client_vm.launch()
        return client_vm

    def do_test_startup(self, kernel_url, initrd_url, kernel_command_line,
                machine_type):
        self.require_accelerator('kvm')

        kernel_path = self.fetch_asset(kernel_url)
        initrd_path = self.fetch_asset(initrd_url)
        socket = os.path.join('/tmp', str(uuid.uuid4()))
        if os.path.exists(socket):
            os.remove(socket)
        self.launch_server_startup(socket)
        client = self.launch_client(kernel_path, initrd_path,
                                    kernel_command_line, machine_type, socket)
        self.validate_vm_launch(client)

    def do_test_hotplug(self, kernel_url, initrd_url, kernel_command_line,
                machine_type):
        self.require_accelerator('kvm')

        kernel_path = self.fetch_asset(kernel_url)
        initrd_path = self.fetch_asset(initrd_url)
        socket = os.path.join('/tmp', str(uuid.uuid4()))
        if os.path.exists(socket):
            os.remove(socket)
        self.launch_server_hotplug(socket)
        client = self.launch_client(kernel_path, initrd_path,
                                    kernel_command_line, machine_type, socket)
        self.validate_vm_launch(client)

    def test_vfio_user_x86_64(self):
        """
        :avocado: tags=arch:x86_64
        :avocado: tags=distro:centos
        """
        kernel_url = ('https://archives.fedoraproject.org/pub/archive/fedora'
                      '/linux/releases/31/Everything/x86_64/os/images'
                      '/pxeboot/vmlinuz')
        initrd_url = ('https://archives.fedoraproject.org/pub/archive/fedora'
                      '/linux/releases/31/Everything/x86_64/os/images'
                      '/pxeboot/initrd.img')
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0 rdinit=/bin/bash')
        machine_type = 'pc'
        self.do_test_startup(kernel_url, initrd_url, kernel_command_line,
                             machine_type)

    def test_vfio_user_aarch64(self):
        """
        :avocado: tags=arch:aarch64
        :avocado: tags=distro:ubuntu
        """
        kernel_url = ('https://archives.fedoraproject.org/pub/archive/fedora'
                      '/linux/releases/31/Everything/aarch64/os/images'
                      '/pxeboot/vmlinuz')
        initrd_url = ('https://archives.fedoraproject.org/pub/archive/fedora'
                      '/linux/releases/31/Everything/aarch64/os/images'
                      '/pxeboot/initrd.img')
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'rdinit=/bin/bash console=ttyAMA0')
        machine_type = 'virt,gic-version=3'
        self.do_test_startup(kernel_url, initrd_url, kernel_command_line,
                             machine_type)

    def test_vfio_user_hotplug_x86_64(self):
        """
        :avocado: tags=arch:x86_64
        :avocado: tags=distro:centos
        """
        kernel_url = ('https://archives.fedoraproject.org/pub/archive/fedora'
                      '/linux/releases/31/Everything/x86_64/os/images'
                      '/pxeboot/vmlinuz')
        initrd_url = ('https://archives.fedoraproject.org/pub/archive/fedora'
                      '/linux/releases/31/Everything/x86_64/os/images'
                      '/pxeboot/initrd.img')
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0 rdinit=/bin/bash')
        machine_type = 'pc'
        self.do_test_hotplug(kernel_url, initrd_url, kernel_command_line,
                             machine_type)

