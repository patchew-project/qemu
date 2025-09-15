#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Reverse debugging test for ppc64
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


class ReverseDebugging_ppc64(QemuSystemTest):

    @skipFlakyTest("https://gitlab.com/qemu-project/qemu/-/issues/1992")
    def test_ppc64_pseries(self):
        self.set_machine('pseries')
        # SLOF branches back to its entry point, which causes this test
        # to take the 'hit a breakpoint again' path. That's not a problem,
        # just slightly different than the other machines.
        reverse_debug(self)

    @skipFlakyTest("https://gitlab.com/qemu-project/qemu/-/issues/1992")
    def test_ppc64_powernv(self):
        self.set_machine('powernv')
        reverse_debug(self)


if __name__ == '__main__':
    QemuSystemTest.main()
