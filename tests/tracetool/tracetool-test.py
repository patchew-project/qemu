#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later

import os
from subprocess import check_output
import sys


def get_backends():
    return [
    ]

def get_formats(backend):
    formats = [
        "c",
        "h",
    ]
    if backend == "dtrace":
        formats += [
            "d",
            "log-stap",
            "simpletrace-stap",
            "stap",
        ]
    if backend == "ust":
        formats += [
            "ust-events-c",
            "ust-events-h",
        ]
    return formats

def test_tracetool_one(tracetool, events, backend, fmt):
    filename = backend + "." + fmt

    args = [tracetool,
            f"--format={fmt}",
            f"--backends={backend}",
            "--group=testsuite"]

    if fmt.find("stap") != -1:
        args += ["--binary=qemu",
                 "--probe-prefix=qemu"]

    args += [events,
             "/dev/stdout"]

    actual = check_output(args)

    if os.getenv("QEMU_TEST_REGENERATE", False):
        print(f"# regenerate {filename}")
        with open(filename, "wb") as fh:
            fh.write(actual)

    with open(filename, "rb") as fh:
        expect = fh.read()

    assert(expect == actual)

def test_tracetool(tracetool, events, backend):
    fail = False
    scenarios = len(get_formats(backend))

    print(f"1..{scenarios}")

    num = 1
    for fmt in get_formats(backend):
        status = "not ok"
        hint = ""
        try:
            test_tracetool_one(tracetool, events, backend, fmt)
            status = "ok"
        except Exception as e:
            print(f"# {e}")
            fail = True
            hint = " (set QEMU_TEST_REGENERATE=1 to recreate reference " + \
                "output if tracetool generator was intentionally changed)"
        finally:
            print(f"{status} {num} - {backend}.{fmt}{hint}")

    return fail


if __name__ == '__main__':
    if len(sys.argv) != 4:
        argv0 = sys.argv[0]
        print("syntax: {argv0} TRACE-TOOL TRACE-EVENTS BACKEND", file=sys.stderr)
        sys.exit(1)

    fail = test_tracetool(sys.argv[1], sys.argv[2], sys.argv[3])
    if fail:
        sys.exit(1)
    sys.exit(0)
