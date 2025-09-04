# SPDX-License-Identifier: GPL-2.0-or-later
#
# Reverse debugging test for ppc64
#
# Copyright (c) 2020 ISP RAS
# Copyright (c) 2025 Linaro Limited
#
# Author:
#  Pavel Dovgalyuk <Pavel.Dovgalyuk@ispras.ru>
#  Gustavo Romero <gustavo.romero@linaro.org> (Run without Avocado)
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

# ReverseDebugging must be imported always first because of the check
# in it for not running this test without the GDB runner.
from reverse_debugging import ReverseDebugging
from qemu_test import skipFlakyTest


class ReverseDebugging_ppc64(ReverseDebugging):

    @skipFlakyTest("https://gitlab.com/qemu-project/qemu/-/issues/1992")
    def test_ppc64_pseries(self):
        self.set_machine('pseries')
        # SLOF branches back to its entry point, which causes this test
        # to take the 'hit a breakpoint again' path. That's not a problem,
        # just slightly different than the other machines.
        self.reverse_debugging()

    @skipFlakyTest("https://gitlab.com/qemu-project/qemu/-/issues/1992")
    def test_ppc64_powernv(self):
        self.set_machine('powernv')
        self.reverse_debugging()


if __name__ == '__main__':
    ReverseDebugging_ppc64.main()
