# Test for the basic qemu-img bench command
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os

from avocado_qemu import QemuImgTest
from avocado.utils import process


class Bench(QemuImgTest):
    """
    Runs the qemu-img tool with the bench command and different
    options and verifies the expected outcome.

    :avocado: enable
    """
    def check_invalid_count(self, count):
        cmd = "%s bench -c %d %s" % (self.qemu_img_bin, count, self.get_data("img"))
        result = process.run(cmd, ignore_status=True)
        self.assertEqual(1, result.exit_status)
        self.assertIn(b"Invalid request count", result.stderr)

    def test_zero_count(self):
        self.check_invalid_count(0)

    def test_negative_count(self):
        self.check_invalid_count(-1)
