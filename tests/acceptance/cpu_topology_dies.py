# Check for crash when using invalid dies value for -smp
#
# Copyright (c) 2020 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.
from avocado_qemu import Test

class CPUTolopogyDies(Test):
    """
    :avocado: tags=arch:x86_64
    :avocado: tags=machine:pc
    """
    def test_invalid(self):
        self.vm.add_args('-S', '-display', 'none', '-smp', '1,dies=0')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        self.vm.wait()
        self.assertEquals(self.vm.exitcode(), 1, "QEMU exit code should be 1")
        self.assertRegex(self.vm.get_log(),
                         r'Invalid CPU topology: dies must be 1 or greater')

    def test_valid(self):
        self.vm.add_args('-S', '-display', 'none', '-smp', '1,dies=1')
        self.vm.launch()
        self.vm.command('quit')
        self.vm.wait()
        self.assertEquals(self.vm.exitcode(), 0, "QEMU exit code should be 0")
