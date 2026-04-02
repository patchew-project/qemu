#!/usr/bin/env python3
#
# Functional test for dynamic QMP monitor hotplug
#
# Copyright (c) 2026 Christian Brauner
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os
import tempfile

from qemu.qmp.legacy import QEMUMonitorProtocol

from qemu_test import QemuSystemTest


class MonitorHotplug(QemuSystemTest):

    def setUp(self):
        super().setUp()
        # Use /tmp to avoid UNIX socket path length limit (108 bytes).
        # The scratch_file() path is too deep for socket names.
        fd, self._sock_path = tempfile.mkstemp(
            prefix='qemu-mon-', suffix='.sock')
        os.close(fd)
        os.unlink(self._sock_path)

    def tearDown(self):
        try:
            os.unlink(self._sock_path)
        except FileNotFoundError:
            pass
        super().tearDown()

    def _add_monitor(self):
        """Create a chardev + monitor and return the socket path."""
        sock = self._sock_path
        self.vm.cmd('chardev-add', id='hotplug-chr', backend={
            'type': 'socket',
            'data': {
                'addr': {
                    'type': 'unix',
                    'data': {'path': sock}
                },
                'server': True,
                'wait': False,
            }
        })
        self.vm.cmd('monitor-add', id='hotplug-mon',
                    chardev='hotplug-chr')
        return sock

    def _remove_monitor(self):
        """Remove the monitor + chardev."""
        self.vm.cmd('monitor-remove', id='hotplug-mon')
        self.vm.cmd('chardev-remove', id='hotplug-chr')

    def _connect_and_handshake(self, sock_path):
        """
        Connect to the dynamic monitor socket, perform the QMP
        greeting and capability negotiation, send a command, then
        disconnect.
        """
        qmp = QEMUMonitorProtocol(sock_path)

        # connect(negotiate=True) receives the greeting, validates it,
        # and sends qmp_capabilities automatically.
        greeting = qmp.connect(negotiate=True)
        self.assertIn('QMP', greeting)
        self.assertIn('version', greeting['QMP'])
        self.assertIn('capabilities', greeting['QMP'])

        # Send a real command to prove the session is fully functional
        resp = qmp.cmd_obj({'execute': 'query-version'})
        self.assertIn('return', resp)
        self.assertIn('qemu', resp['return'])

        qmp.close()

    def test_hotplug_cycle(self):
        """
        Hotplug a monitor, do the full QMP handshake, unplug it,
        then repeat the whole cycle a second time.
        """
        self.set_machine('none')
        self.vm.add_args('-nodefaults')
        self.vm.launch()

        # First cycle
        sock = self._add_monitor()
        self._connect_and_handshake(sock)
        self._remove_monitor()

        # Second cycle -- same ids, same path, must work
        sock = self._add_monitor()
        self._connect_and_handshake(sock)
        self._remove_monitor()


if __name__ == '__main__':
    QemuSystemTest.main()
