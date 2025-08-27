#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# i386 migration test

from migration import MigrationTest


class I386MigrationTest(MigrationTest):

    def test_migration_with_tcp_localhost(self):
        self.set_machine('isapc')
        self.migration_with_tcp_localhost()

    def test_migration_with_unix(self):
        self.set_machine('isapc')
        self.migration_with_unix()

    def test_migration_with_exec(self):
        self.set_machine('isapc')
        self.migration_with_exec()


if __name__ == '__main__':
    MigrationTest.main()
