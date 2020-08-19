#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
This takes a crashing qtest trace and tries to remove superflous operations
"""

import sys
import os
import subprocess
import time

QEMU_ARGS = None
QEMU_PATH = None
TIMEOUT = 5
CRASH_TOKEN = None


def usage():
    sys.exit("""\
Usage: QEMU_PATH="/path/to/qemu" QEMU_ARGS="args" {} input_trace output_trace
By default, will try to use the second-to-last line in the output to identify
whether the crash occred. Optionally, manually set a string that idenitifes the
crash by setting CRASH_TOKEN=
""".format((sys.argv[0])))


def check_if_trace_crashes(trace, path):
    global CRASH_TOKEN
    with open(path, "w") as tracefile:
        tracefile.write("".join(trace))
    rc = subprocess.Popen("timeout -s 9 {}s {} {} 2>&1 < {}".format(TIMEOUT,
                          QEMU_PATH, QEMU_ARGS, path),
                          shell=True, stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE)
    stdo = rc.communicate()[0]
    output = stdo.decode('unicode_escape')
    if rc.returncode == 137:    # Timed Out
        return False
    if len(output.splitlines()) < 2:
        return False

    if CRASH_TOKEN is None:
        CRASH_TOKEN = output.splitlines()[-2]

    return CRASH_TOKEN in output


def minimize_trace(inpath, outpath):
    global TIMEOUT
    with open(inpath) as f:
        trace = f.readlines()
    start = time.time()
    if not check_if_trace_crashes(trace, outpath):
        sys.exit("The input qtest trace didn't cause a crash...")
    end = time.time()
    print("Crashed in {} seconds".format(end-start))
    TIMEOUT = (end-start)*5
    print("Setting the timeout for {} seconds".format(TIMEOUT))
    print("Identifying Crashes by this string: {}".format(CRASH_TOKEN))

    i = 0
    newtrace = trace[:]
    while i < len(newtrace):
        prior = newtrace[i]
        print("Trying to remove {}".format(newtrace[i]))
        # Try to remove the line completely
        newtrace[i] = ""
        if check_if_trace_crashes(newtrace, outpath):
            i += 1
            continue
        newtrace[i] = prior
        # Try to split up writes into multiple commands, each of which can be
        # removed.
        if newtrace[i].startswith("write "):
            addr = int(newtrace[i].split()[1], 16)
            length = int(newtrace[i].split()[2], 16)
            data = newtrace[i].split()[3][2:]
            if length > 1:
                leftlength = int(length/2)
                rightlength = length - leftlength
                newtrace.insert(i+1, "")
                while leftlength > 0:
                    newtrace[i] = "write {} {} 0x{}\n".format(
                            hex(addr),
                            hex(leftlength),
                            data[:leftlength*2])
                    newtrace[i+1] = "write {} {} 0x{}\n".format(
                            hex(addr+leftlength),
                            hex(rightlength),
                            data[leftlength*2:])
                    if check_if_trace_crashes(newtrace, outpath):
                        break
                    else:
                        leftlength -= 1
                        rightlength += 1
                if check_if_trace_crashes(newtrace, outpath):
                    i -= 1
                else:
                    newtrace[i] = prior
                    del newtrace[i+1]
        i += 1
    check_if_trace_crashes(newtrace, outpath)


if __name__ == '__main__':
    if len(sys.argv) < 3:
        usage()

    QEMU_PATH = os.getenv("QEMU_PATH")
    QEMU_ARGS = os.getenv("QEMU_ARGS")
    if QEMU_PATH is None or QEMU_ARGS is None:
        usage()
    if "accel" not in QEMU_ARGS:
        QEMU_ARGS += " -accel qtest"
    CRASH_TOKEN = os.getenv("CRASH_TOKEN")
    QEMU_ARGS += " -qtest stdio -monitor none -serial none "
    minimize_trace(sys.argv[1], sys.argv[2])
