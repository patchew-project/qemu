#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
This takes a crashing qtest trace and tries to remove superflous operations
"""

import sys
import os
import subprocess
import time
import struct
import re

QEMU_ARGS = None
QEMU_PATH = None
TIMEOUT = 5

crash_patterns = ("Assertion.+failed",
                  "SUMMARY.+Sanitizer")
crash_pattern = None
crash_string = None

write_suffix_lookup = {"b": (1, "B"),
                       "w": (2, "H"),
                       "l": (4, "L"),
                       "q": (8, "Q")}

def usage():
    sys.exit("""\
Usage: QEMU_PATH="/path/to/qemu" QEMU_ARGS="args" {} input_trace output_trace
By default, we will try to search predefined crash patterns through the
tracing output to see whether the crash occred. Optionally, manually set a
string that idenitifes the crash by setting CRASH_PATTERN=
""".format((sys.argv[0])))

def check_if_trace_crashes(trace, path):
    with open(path, "w") as tracefile:
        tracefile.write("".join(trace))

    rc = subprocess.Popen("timeout -s 9 {timeout}s {qemu_path} {qemu_args} 2>&1\
    < {trace_path}".format(timeout=TIMEOUT,
                           qemu_path=QEMU_PATH,
                           qemu_args=QEMU_ARGS,
                           trace_path=path),
                          shell=True,
                          stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE)
    if rc.returncode == 137:    # Timed Out
        return False

    stdo = rc.communicate()[0]
    output = stdo.decode('unicode_escape')
    output_lines = output.splitlines()
    # Usually we care about the summary info in the last few lines, reverse.
    output_lines.reverse()

    global crash_pattern, crash_patterns, crash_string
    if crash_pattern is None: # Initialization
        for line in output_lines:
            for c in crash_patterns:
                if re.search(c, line) is not None:
                    crash_pattern = c
                    crash_string = line
                    print("Identifying crash pattern by this string: ",\
                          crash_string)
                    print("Using regex pattern: ", crash_pattern)
                    return True
        print("Failed to initialize crash pattern: no match.")
        return False

    # First, we search exactly the previous crash string.
    for line in output_lines:
        if crash_string == line:
            return True

    # Then we decide whether a similar (same pattern) crash happened.
    # Slower now :(
    for line in output_lines:
        if re.search(crash_pattern, line) is not None:
            print("\nINFO: The crash string changed during our minimization process.")
            print("Before: ", crash_string)
            print("After: ", line)
            print("The original regex pattern can still match, updated the crash string.")
            crash_string = line
            return True

    # The input did not trigger (the same type) bug.
    return False


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

    i = 0
    newtrace = trace[:]
    # For each line
    while i < len(newtrace):
        # 1.) Try to remove it completely and reproduce the crash. If it works,
        # we're done.
        prior = newtrace[i]
        print("Trying to remove {}".format(newtrace[i]))
        # Try to remove the line completely
        newtrace[i] = ""
        if check_if_trace_crashes(newtrace, outpath):
            i += 1
            continue
        newtrace[i] = prior

        # 2.) Try to replace write{bwlq} commands with a write addr, len
        # command. Since this can require swapping endianness, try both LE and
        # BE options. We do this, so we can "trim" the writes in (3)
        if (newtrace[i].startswith("write") and not
            newtrace[i].startswith("write ")):
            suffix = newtrace[i].split()[0][-1]
            assert(suffix in write_suffix_lookup)
            addr = int(newtrace[i].split()[1], 16)
            value = int(newtrace[i].split()[2], 16)
            for endianness in ['<', '>']:
                data = struct.pack("{end}{size}".format(end=endianness,
                                   size=write_suffix_lookup[suffix][1]),
                                   value)
                newtrace[i] = "write {addr} {size} 0x{data}\n".format(
                    addr=hex(addr),
                    size=hex(write_suffix_lookup[suffix][0]),
                    data=data.hex())
                if(check_if_trace_crashes(newtrace, outpath)):
                    break
            else:
                newtrace[i] = prior

        # 3.) If it is a qtest write command: write addr len data, try to split
        # it into two separate write commands. If splitting the write down the
        # rightmost does not work, try to move the pivot "left" and retry, until
        # there is no space left. The idea is to prune unneccessary bytes from
        # long writes, while accommodating arbitrary MemoryRegion access sizes
        # and alignments.
        if newtrace[i].startswith("write "):
            addr = int(newtrace[i].split()[1], 16)
            length = int(newtrace[i].split()[2], 16)
            data = newtrace[i].split()[3][2:]
            if length > 1:
                leftlength = length - 1
                rightlength = length - leftlength
                newtrace.insert(i+1, "")
                while leftlength > 0:
                    newtrace[i] = "write {addr} {size} 0x{data}\n".format(
                            addr=hex(addr),
                            size=hex(leftlength),
                            data=data[:leftlength*2])
                    newtrace[i+1] = "write {addr} {size} 0x{data}\n".format(
                            addr=hex(addr+leftlength),
                            size=hex(rightlength),
                            data=data[leftlength*2:])
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

    assert(check_if_trace_crashes(newtrace, outpath))

    TIMEOUT = (end-start)*2 # input is short now

    # try setting bits in operands of out/write to zero
    i = 0
    while i < len(newtrace):
        if (not newtrace[i].startswith("write ") and not
           newtrace[i].startswith("out")):
           i += 1
           continue
        # write ADDR SIZE DATA
        # outx ADDR VALUE
        print("\nzero setting bits: {}".format(newtrace[i]))

        prefix = " ".join(newtrace[i].split()[:-1])
        data = newtrace[i].split()[-1]
        data_bin = bin(int(data, 16))
        data_bin_list = list(data_bin)

        for j in range(2, len(data_bin_list)):
            prior = newtrace[i]
            if (data_bin_list[j] == '1'):
                data_bin_list[j] = '0'
                data_try = hex(int("".join(data_bin_list), 2))
                # It seems qtest only accect hex with one byte zero padding
                if len(data_try) % 2 == 1:
                    data_try = data_try[:2] + "0" + data_try[2:-1]

                newtrace[i] = "{prefix} {data_try}\n".format(
                        prefix=prefix,
                        data_try=data_try)

                if not check_if_trace_crashes(newtrace, outpath):
                    data_bin_list[j] = '1'
                    newtrace[i] = prior

        i += 1

    assert(check_if_trace_crashes(newtrace, outpath))

    # delay IO instructions until they can't trigger the crash
    # Note: O(n^2) and many timeouts, kinda slow
    i = len(newtrace) - 1
    while i >= 0:
        tmp_i = newtrace[i]
        if len(tmp_i) < 2:
            i -= 1
            continue
        print("Delaying ", newtrace[i])
        for j in reversed(range(i+1, len(newtrace)+1)):
            newtrace.insert(j, tmp_i)
            del newtrace[i]
            if check_if_trace_crashes(newtrace, outpath):
                break
            newtrace.insert(i, tmp_i)
            del newtrace[j]
        i -= 1

    assert(check_if_trace_crashes(newtrace, outpath))
    # maybe another removing round


if __name__ == '__main__':
    if len(sys.argv) < 3:
        usage()

    QEMU_PATH = os.getenv("QEMU_PATH")
    QEMU_ARGS = os.getenv("QEMU_ARGS")
    if QEMU_PATH is None or QEMU_ARGS is None:
        usage()
    # if "accel" not in QEMU_ARGS:
    #     QEMU_ARGS += " -accel qtest"
    crash_pattern = os.getenv("CRASH_PATTERN")
    QEMU_ARGS += " -qtest stdio -monitor none -serial none "
    minimize_trace(sys.argv[1], sys.argv[2])
