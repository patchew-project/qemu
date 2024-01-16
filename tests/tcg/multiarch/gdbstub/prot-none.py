"""Test that GDB can access PROT_NONE pages.

This runs as a sourced script (via -x, via run-test.py).

SPDX-License-Identifier: GPL-2.0-or-later
"""
from test_gdbstub import main, report


def run_test():
    """Run through the tests one by one"""
    gdb.Breakpoint("break_here")
    gdb.execute("continue")
    val = gdb.parse_and_eval("*(char[2] *)q").string()
    report(val == "42", "{} == 42".format(val))
    gdb.execute("set *(char[3] *)q = \"24\"")
    gdb.execute("continue")
    exitcode = int(gdb.parse_and_eval("$_exitcode"))
    report(exitcode == 0, "{} == 0".format(exitcode))


main(run_test)
