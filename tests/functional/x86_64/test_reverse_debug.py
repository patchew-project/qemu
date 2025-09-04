# SPDX-License-Identifier: GPL-2.0-or-later
#
# Reverse debugging test for x86_64
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


class ReverseDebugging_X86_64(ReverseDebugging):

    @skipFlakyTest("https://gitlab.com/qemu-project/qemu/-/issues/2922")
    def test_x86_64_pc(self):
        self.set_machine('pc')
        # Start with BIOS only
        self.reverse_debugging()


if __name__ == '__main__':
    ReverseDebugging_X86_64.main()
