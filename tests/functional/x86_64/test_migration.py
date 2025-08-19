#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# x86_64 migration test

from migration import MigrationTest


class X8664MigrationTest(MigrationTest):

    def test_migration_with_tcp_localhost(self):
        self.set_machine('microvm')
        self.migration_with_tcp_localhost()

    def test_migration_with_unix(self):
        self.set_machine('microvm')
        self.migration_with_unix()

    def test_migration_with_exec(self):
        self.set_machine('microvm')
        self.migration_with_exec()


if __name__ == '__main__':
    MigrationTest.main()
