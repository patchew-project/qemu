from __future__ import print_function
#
# Test the SME registers are visible and changeable via gdbstub
#
# This is launched via tests/guest-debug/run-test.py
#

import argparse
import gdb
from test_gdbstub import main, report

MAGIC = 0x01020304

def run_test():
    "Run through the tests one by one"

    frame = gdb.selected_frame()
    rname = "za"
    za = frame.read_register(rname)
    report(True, "Reading %s" % rname)

    for i in range(0, 16):
        for j in range(0, 16):
            cmd = "set $za[%d][%d] = 0x01" % (i, j)
            gdb.execute(cmd)
            report(True, "%s" % cmd)
    for i in range(0, 16):
        for j in range(0, 16):
            reg = "$za[%d][%d]" % (i, j)
            v = gdb.parse_and_eval(reg)
            report(str(v.type) == "uint8_t",
                    "size of %s" % (reg))
            report(int(v) == 0x1, "%s is 0x%x" % (reg, 0x1))

def run_test_slices():
    "Run through the tests one by one"

    frame = gdb.selected_frame()
    rname = "za"
    za = frame.read_register(rname)
    report(True, "Reading %s" % rname)

    for i in range(0, 16):
        for j in range(0, 16):
            cmd = "set $za[%d][%d] = 0x01" % (i, j)
            gdb.execute(cmd)
            report(True, "%s" % cmd)
    for i in range(0, 16):
        for j in range(0, 16):
            reg = "$za[%d][%d]" % (i, j)
            v = gdb.parse_and_eval(reg)
            report(str(v.type) == "uint8_t",
                    "size of %s" % (reg))
            report(int(v) == 0x1, "%s is 0x%x" % (reg, 0x1))

    for i in range(0, 4):
        for j in range(0, 4):
            for k in range(0, 4):
                cmd = "set $za%dhq%d[%d] = 0x%x" % (i, j, k, MAGIC)
                gdb.execute(cmd)
                report(True, "%s" % cmd)
        for j in range(0, 4):
            for k in range(0, 4):
                reg = "$za%dhq%d[%d]" % (i, j, k)
                v = gdb.parse_and_eval(reg)
                report(str(v.type) == "uint128_t",
                    "size of %s" % (reg))
                report(int(v) == MAGIC, "%s is 0x%x" % (reg, MAGIC))
        
        for j in range(0, 4):
            for k in range(0, 4):
                cmd = "set $za%dvq%d[%d] = 0x%x" % (i, j, k, MAGIC)
                gdb.execute(cmd)
                report(True, "%s" % cmd)
        for j in range(0, 4):
            for k in range(0, 4):
                reg = "$za%dvq%d[%d]" % (i, j, k)
                v = gdb.parse_and_eval(reg)
                report(str(v.type) == "uint128_t",
                    "size of %s" % (reg))
                report(int(v) == MAGIC, "%s is 0x%x" % (reg, MAGIC))

    for i in range(0, 4):
        for j in range(0, 4):
            for k in range(0, 4):
                cmd = "set $za%dhd%d[%d] = 0x%x" % (i, j, k, MAGIC)
                gdb.execute(cmd)
                report(True, "%s" % cmd)
        for j in range(0, 4):
            for k in range(0, 4):
                reg = "$za%dhd%d[%d]" % (i, j, k)
                v = gdb.parse_and_eval(reg)
                report(str(v.type) == "uint64_t",
                    "size of %s" % (reg))
                report(int(v) == MAGIC, "%s is 0x%x" % (reg, MAGIC))
        
        for j in range(0, 4):
            for k in range(0, 4):
                cmd = "set $za%dvd%d[%d] = 0x%x" % (i, j, k, MAGIC)
                gdb.execute(cmd)
                report(True, "%s" % cmd)
        for j in range(0, 4):
            for k in range(0, 4):
                reg = "$za%dvd%d[%d]" % (i, j, k)
                v = gdb.parse_and_eval(reg)
                report(str(v.type) == "uint64_t",
                    "size of %s" % (reg))
                report(int(v) == MAGIC, "%s is 0x%x" % (reg, MAGIC))


parser = argparse.ArgumentParser(description="A gdbstub test for SME support")
parser.add_argument("--gdb_sme_tile_support", help="GDB support for SME tiles", \
                    action="store_true")
args = parser.parse_args()

if args.gdb_sme_tile_support:
    main(run_test_slices, expected_arch="aarch64")
else:
    main(run_test, expected_arch="aarch64")