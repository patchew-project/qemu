#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Test the BQL watchdog against a main thread blocked on IO.
#
# Copyright (C) 2026 Virtuozzo International GmbH

from qemu_test import QemuSystemTest

deadline_ms = 100
hold_ns = 1000 * 1000 * 1000


class BqlWatchdog(QemuSystemTest):

    def setUp(self):
        super().setUp()
        self.set_machine('none')
        self.vm.add_args('-nodefaults')
        self.vm.add_args('-blockdev',
                         f'driver=null-co,node-name=null0,size=1048576,'
                         f'latency-ns={hold_ns}')
        self.vm.launch()

    def arm(self, ms):
        self.vm.cmd('qom-set', path='/machine',
                    property='bql-watchdog-ms', value=ms)

    def hold_the_bql(self):
        """
        human-monitor-command is not a coroutine command, so the write runs
        to completion under AIO_WAIT_WHILE() in the main thread, which keeps
        the BQL for as long as null-co takes to answer.
        """
        self.vm.cmd('human-monitor-command',
                    command_line='qemu-io null0 "write 0 4k"')

    def log_after_shutdown(self):
        """The log only reaches the harness once the VM is reaped"""
        self.vm.shutdown()
        return self.vm.get_log() or ''

    def test_reports_a_hold(self):
        self.arm(deadline_ms)
        self.hold_the_bql()
        self.arm(0)

        log = self.log_after_shutdown()
        self.assertIn(f'BQL held for more than {deadline_ms} ms', log)

    def test_silent_while_disarmed(self):
        self.hold_the_bql()

        log = self.log_after_shutdown()
        self.assertNotIn('BQL held for more than', log)


if __name__ == '__main__':
    QemuSystemTest.main()
