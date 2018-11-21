# Tests for QMP set-numa-node related behavior and regressions
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from avocado_qemu import Test


class SetNumaNode(Test):
    """
    :avocado: enable
    :avocado: tags=quick,numa
    """
    def test_numa_not_supported(self):
        self.vm.add_args('-nodefaults', '-S', '-preconfig')
        self.vm.set_machine('none')
        self.vm.launch()
        res = self.vm.qmp('set-numa-node', type='node')
        self.assertIsNotNone(res, 'Unexpected empty QMP response to "set-numa-node"')
        self.assertEqual(res['error']['class'], 'GenericError')
        self.assertEqual(res['error']['desc'],
                         'NUMA is not supported by this machine-type')
        self.assertTrue(self.vm.is_running())
        self.vm.qmp('x-exit-preconfig')
        self.vm.shutdown()
        self.assertEqual(self.vm.exitcode(), 0)

    def test_no_preconfig(self):
        self.vm.add_args('-nodefaults', '-S')
        self.vm.set_machine('none')
        self.vm.launch()
        res = self.vm.qmp('set-numa-node', type='node')
        self.assertIsNotNone(res, 'Unexpected empty QMP response to "set-numa-node"')
        self.assertEqual(res['error']['class'], 'GenericError')
        self.assertEqual(res['error']['desc'],
                         "The command is permitted only in 'preconfig' state")
