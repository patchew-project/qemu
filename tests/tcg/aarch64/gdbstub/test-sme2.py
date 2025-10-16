#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: GPL-2.0-or-later

#
# Test the SME registers are visible and changeable via gdbstub
#
# This is launched via tests/guest-debug/run-test.py
#

import argparse
import gdb
from test_gdbstub import main, report

MAGIC = 0x01020304
BASIC_ZA_TEST = 0
TILE_SLICE_TEST = 0


def run_test():
    """Test reads and writes of the SME2 ZT0 register"""
    frame = gdb.selected_frame()
    rname = "zt0"
    zt0 = frame.read_register(rname)
    report(True, "Reading %s" % rname)

    # Writing to the ZT0 register, byte by byte.
    for i in range(0, 64):
        cmd = "set $zt0[%d] = 0x01" % (i)
            gdb.execute(cmd)
            report(True, "%s" % cmd)

    # Reading from the ZT0 register, byte by byte.
    for i in range(0, 64):
        reg = "$zt0[%d]" % (i)
        v = gdb.parse_and_eval(reg)
        report(str(v.type) == "uint8_t", "size of %s" % (reg))
        report(v == 0x1, "%s is 0x%x" % (reg, 0x1))

main(run_test, expected_arch="aarch64")
