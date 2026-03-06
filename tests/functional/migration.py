# SPDX-License-Identifier: GPL-2.0-or-later
#
# Migration test base class
#
# Copyright (c) 2019 Red Hat, Inc.
#
# Authors:
#  Cleber Rosa <crosa@redhat.com>
#  Caio Carrara <ccarrara@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import tempfile
import time

from qemu_test import QemuSystemTest, which
from qemu_test.ports import Ports


class MigrationTest(QemuSystemTest):

    timeout = 10

    @staticmethod
    def migration_finished(vm):
        return vm.cmd('query-migrate')['status'] in ('completed', 'failed')

    def assert_migration(self, src_vm, dst_vm):

        end = time.monotonic() + self.timeout
        while time.monotonic() < end and not self.migration_finished(src_vm):
            time.sleep(0.1)

        end = time.monotonic() + self.timeout
        while time.monotonic() < end and not self.migration_finished(dst_vm):
            time.sleep(0.1)

        self.assertEqual(src_vm.cmd('query-migrate')['status'], 'completed')
        self.assertEqual(dst_vm.cmd('query-migrate')['status'], 'completed')
        self.assertEqual(dst_vm.cmd('query-status')['status'], 'running')
        self.assertEqual(src_vm.cmd('query-status')['status'],'postmigrate')

    # Can be overridden by subclasses to configure both source/dest VMs.
    def configure_machine(self, vm):
        vm.add_args('-nodefaults')

    # Can be overridden by subclasses to prepare the source VM before migration,
    # e.g. by running some workload inside the source VM to see if it continues
    # to run properly after migration.
    def launch_source_vm(self, vm):
        vm.launch()

    # Can be overridden by subclasses to check the destination VM after migration,
    # e.g. by checking if the workload is still running after migration.
    def assert_dest_vm(self, vm):
        pass

    def do_migrate(self, dest_uri, src_uri=None):
        dest_vm = self.get_vm('-incoming', dest_uri, name="dest-qemu")
        self.configure_machine(dest_vm)
        dest_vm.launch()
        if src_uri is None:
            src_uri = dest_uri
        source_vm = self.get_vm(name="source-qemu")
        self.configure_machine(source_vm)
        self.launch_source_vm(source_vm)
        source_vm.qmp('migrate', uri=src_uri)
        self.assert_migration(source_vm, dest_vm)
        self.assert_dest_vm(dest_vm)

    def _get_free_port(self, ports):
        port = ports.find_free_port()
        if port is None:
            self.skipTest('Failed to find a free port')
        return port

    def migration_with_tcp_localhost(self):
        with Ports() as ports:
            dest_uri = 'tcp:localhost:%u' % self._get_free_port(ports)
            self.do_migrate(dest_uri)

    def migration_with_unix(self):
        with tempfile.TemporaryDirectory(prefix='socket_') as socket_path:
            dest_uri = 'unix:%s/qemu-test.sock' % socket_path
            self.do_migrate(dest_uri)

    def migration_with_exec(self):
        if not which('ncat'):
            self.skipTest('ncat is not available')
        with Ports() as ports:
            free_port = self._get_free_port(ports)
            dest_uri = 'exec:ncat -l localhost %u' % free_port
            src_uri = 'exec:ncat localhost %u' % free_port
            self.do_migrate(dest_uri, src_uri)
