# SPDX-License-Identifier: GPL-2.0-or-later
#
# Migration test
#
# Copyright (c) 2019 Red Hat, Inc.
#
# Authors:
#  Cleber Rosa <crosa@redhat.com>
#  Caio Carrara <ccarrara@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import time


class Migration():

    @staticmethod
    def migration_finished(vm):
        return vm.cmd('query-migrate')['status'] in ('completed', 'failed')

    def assert_migration(self, test, src_vm, dst_vm, timeout):

        end = time.monotonic() + timeout
        while time.monotonic() < end and not self.migration_finished(src_vm):
           time.sleep(0.1)

        end = time.monotonic() + timeout
        while time.monotonic() < end and not self.migration_finished(dst_vm):
           time.sleep(0.1)

        test.assertEqual(src_vm.cmd('query-migrate')['status'], 'completed')
        test.assertEqual(dst_vm.cmd('query-migrate')['status'], 'completed')
        test.assertEqual(dst_vm.cmd('query-status')['status'], 'running')
        test.assertEqual(src_vm.cmd('query-status')['status'],'postmigrate')

    def migrate(self, test, source_vm, dest_vm, src_uri, timeout):
        source_vm.qmp('migrate', uri=src_uri)
        self.assert_migration(test, source_vm, dest_vm, timeout)
