# Simple functional tests for VNC functionality
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os
import tempfile
import shutil

from avocado_qemu import Test


class Vnc(Test):
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
        set_password_response = self.vm.qmp('change',
                                            device='vnc',
                                            target='password',
                                            arg='new_password')
        self.assertIn('error', set_password_response)
        self.assertEqual(set_password_response['error']['class'],
                         'GenericError')
        self.assertEqual(set_password_response['error']['desc'],
                         'Could not set password')

class VncUnixSocket(Test):

    def setUp(self):
        super(VncUnixSocket, self).setUp()
        self.socket_dir = tempfile.mkdtemp()
        self.socket_path = os.path.join(self.socket_dir, 'vnc-socket')

    def test_vnc_change_password_requires_a_password(self):
        self.vm.add_args('-nodefaults', '-S',
                         '-vnc', 'unix:%s' % self.socket_path)
        self.vm.launch()
        self.assertTrue(self.vm.qmp('query-vnc')['return']['enabled'])
        set_password_response = self.vm.qmp('change',
                                            device='vnc',
                                            target='password',
                                            arg='new_password')
        self.assertIn('error', set_password_response)
        self.assertEqual(set_password_response['error']['class'],
                         'GenericError')
        self.assertEqual(set_password_response['error']['desc'],
                         'Could not set password')

    def test_vnc_change_password(self):
        self.vm.add_args('-nodefaults', '-S',
                         '-vnc', 'unix:%s,password' % self.socket_path)
        self.vm.launch()
        self.assertTrue(self.vm.qmp('query-vnc')['return']['enabled'])
        set_password_response = self.vm.qmp('change',
                                            device='vnc',
                                            target='password',
                                            arg='new_password')
        self.assertEqual(set_password_response['return'], {})

    def tearDown(self):
        shutil.rmtree(self.socket_dir)
