# Simple functional tests for VNC functionality
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from avocado_qemu import QemuSystemTest


class Vnc(QemuSystemTest):
    """
    :avocado: tags=vnc,quick
    """
    def test_no_vnc(self):
        self.vm.add_args('-nodefaults', '-S')
        self.vm.launch()
        self.assertFalse(self.vm.qmp('query-vnc')['return']['enabled'])

    def test_no_vnc_change_password(self):
        self.vm.add_args('-nodefaults', '-S')
        self.vm.launch()
        self.assertFalse(self.vm.qmp('query-vnc')['return']['enabled'])
        set_password_response = self.vm.qmp('change-vnc-password',
                                            password='new_password')
        self.assertIn('error', set_password_response)
        self.assertEqual(set_password_response['error']['class'],
                         'GenericError')
        self.assertEqual(set_password_response['error']['desc'],
                         'Could not set password')

    def test_change_password_requires_a_password(self):
        self.vm.add_args('-nodefaults', '-S', '-vnc', ':0')
        self.vm.launch()
        self.assertTrue(self.vm.qmp('query-vnc')['return']['enabled'])
        set_password_response = self.vm.qmp('change-vnc-password',
                                            password='new_password')
        self.assertIn('error', set_password_response)
        self.assertEqual(set_password_response['error']['class'],
                         'GenericError')
        self.assertEqual(set_password_response['error']['desc'],
                         'Could not set password')

    def test_change_password(self):
        self.vm.add_args('-nodefaults', '-S', '-vnc', ':0,password=on')
        self.vm.launch()
        self.assertTrue(self.vm.qmp('query-vnc')['return']['enabled'])
        set_password_response = self.vm.qmp('change-vnc-password',
                                            password='new_password')
        self.assertEqual(set_password_response['return'], {})

    def test_change_listen(self):
        self.vm.add_args('-nodefaults', '-S', '-vnc', ':0')
        self.vm.launch()
        self.assertEqual(self.vm.qmp('query-vnc')['return']['service'], '5900')
        res = self.vm.qmp('change-vnc-listen', id='default',
                          addresses=[{'type': 'inet', 'host': '0.0.0.0',
                                      'port': '5901'}])
        self.assertEqual(res['return'], {})
        self.assertEqual(self.vm.qmp('query-vnc')['return']['service'], '5901')
