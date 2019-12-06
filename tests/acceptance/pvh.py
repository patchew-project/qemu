# Copyright (c) 2019 Red Hat, Inc.
#
# Author:
#  Wainer dos Santos Moschetta <wainersm@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

"""
x86/HVM direct boot acceptance tests.
"""

from avocado.utils.kernel import KernelBuild
from avocado_qemu import Test
from avocado_qemu import wait_for_console_pattern


class Pvh(Test):
    """
    Test suite for x86/HVM direct boot feature.

    :avocado: tags=slow,arch=x86_64,machine=q35
    """
    def test_boot_vmlinux(self):
        """
        Boot uncompressed kernel image.
        """
        # QEMU can boot a vmlinux image for kernel >= 4.21 built
        # with CONFIG_PVH=y
        kernel_version = '5.4.1'
        kbuild = KernelBuild(kernel_version, work_dir=self.workdir)
        try:
            kbuild.download()
            kbuild.uncompress()
            kbuild.configure(targets=['defconfig', 'kvmconfig'],
                             extra_configs=['CONFIG_PVH=y'])
            kbuild.build()
        except:
            self.cancel("Unable to build vanilla kernel %s" % kernel_version)

        self.vm.set_machine('q35')
        self.vm.set_console()
        kernel_command_line = 'printk.time=0 console=ttyS0'
        self.vm.add_args('-kernel', kbuild.vmlinux,
                         '-append', kernel_command_line)
        self.vm.launch()
        wait_for_console_pattern(self, 'Kernel command line: %s' %
                                 kernel_command_line)
