#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Reverse debugging test for x86_64
#
# Copyright (c) 2020 ISP RAS
#
# Author:
#  Pavel Dovgalyuk <Pavel.Dovgalyuk@ispras.ru>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import QemuSystemTest, skipFlakyTest

from reverse_debugging import reverse_debug


class ReverseDebugging_X86_64(QemuSystemTest):

    @skipFlakyTest("https://gitlab.com/qemu-project/qemu/-/issues/2922")
    def test_x86_64_pc(self):
        self.set_machine('pc')
        # Start with BIOS only
        reverse_debug(self)


if __name__ == '__main__':
    QemuSystemTest.main()
