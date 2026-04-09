#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Functional test for dynamic QMP monitor hotplug
#
# Copyright (c) 2026 Christian Brauner

import os

from qemu_test import QemuSystemTest

from qemu.qmp.legacy import QEMUMonitorProtocol


class MonitorHotplug(QemuSystemTest):

    def setUp(self):
        super().setUp()
        sock_dir = self.socket_dir()
        self._sock_path = os.path.join(sock_dir.name, 'hotplug.sock')

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

    def test_self_removal(self):
        """
        A dynamically-added monitor sends monitor-remove targeting
        itself.  Verify the response is delivered before the
        connection drops, and that the monitor is gone afterwards.
        """
        self.set_machine('none')
        self.vm.add_args('-nodefaults')
        self.vm.launch()

        sock = self._add_monitor()

        qmp = QEMUMonitorProtocol(sock)
        greeting = qmp.connect(negotiate=True)
        self.assertIn('QMP', greeting)

        # Self-removal: the dynamic monitor removes itself
        resp = qmp.cmd_obj({'execute': 'monitor-remove',
                            'arguments': {'id': 'hotplug-mon'}})
        self.assertIn('return', resp)

        qmp.close()

        # The main monitor should no longer list the removed monitor
        monitors = self.vm.cmd('query-monitors')
        for m in monitors:
            self.assertNotEqual(m.get('id'), 'hotplug-mon')

        # Clean up the chardev
        self.vm.cmd('chardev-remove', id='hotplug-chr')

    def test_large_response(self):
        """
        Send a command with a large response (query-qmp-schema) on a
        dynamically-added monitor to exercise the output buffer flush
        path.
        """
        self.set_machine('none')
        self.vm.add_args('-nodefaults')
        self.vm.launch()

        sock = self._add_monitor()

        qmp = QEMUMonitorProtocol(sock)
        qmp.connect(negotiate=True)

        resp = qmp.cmd_obj({'execute': 'query-qmp-schema'})
        self.assertIn('return', resp)
        self.assertIsInstance(resp['return'], list)
        self.assertGreater(len(resp['return']), 0)

        qmp.close()
        self._remove_monitor()

    def test_events_after_negotiation(self):
        """
        Verify that QMP events are delivered on a dynamically-added
        monitor after capability negotiation completes.
        """
        self.set_machine('none')
        self.vm.add_args('-nodefaults')
        self.vm.launch()

        sock = self._add_monitor()

        qmp = QEMUMonitorProtocol(sock)
        qmp.connect(negotiate=True)

        # Trigger a STOP event via the main monitor, then read it
        # from the dynamic monitor.
        self.vm.cmd('stop')
        resp = qmp.pull_event(wait=True)
        self.assertEqual(resp['event'], 'STOP')

        self.vm.cmd('cont')
        resp = qmp.pull_event(wait=True)
        self.assertEqual(resp['event'], 'RESUME')

        qmp.close()
        self._remove_monitor()


if __name__ == '__main__':
    QemuSystemTest.main()
