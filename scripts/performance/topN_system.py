#!/usr/bin/env python3

"""
Print the top N most executed functions in QEMU system mode emulation.

Syntax:
topN_system.py [-h] [-n <number of displayed top functions>] -- \
    <qemu system executable> <qemu system options>

[-h] - Print the script arguments help message.
[-n] - Specify the number of top functions to print.
     - If this flag is not specified, the tool defaults to 25.

Example of usage:
topN_system.py -n 20 -- qemu-system-x86_64 -m 1024 -kernel <kernel> \
    -initrd <initrd>

This file is a part of the project "TCG Continuous Benchmarking".

Copyright (C) 2020  Ahmed Karaman <ahmedkhaledkaraman@gmail.com>
Copyright (C) 2020  Aleksandar Markovic <aleksandar.qemu.devel@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>.
"""

import argparse
import os
import subprocess
import sys
import tempfile


# Parse the command line arguments
PARSER = argparse.ArgumentParser(
    usage="usage: topN_system.py [-h] [-n <number of displayed top functions>]"
          " -- <qemu system executable> <qemu system options>")

PARSER.add_argument("-n", dest="top", type=int, default=25,
                    help="Specify the number of top functions to print.")

PARSER.add_argument("command", type=str, nargs="+", help=argparse.SUPPRESS)

ARGS = PARSER.parse_args()

# Extract the needed variables from the args
COMMAND = ARGS.command
TOP = ARGS.top

# Ensure that perf is installed
CHECK_PERF_PRESENCE = subprocess.run(["which", "perf"],
                                     stdout=subprocess.DEVNULL,
                                     check=False)
if CHECK_PERF_PRESENCE.returncode:
    sys.exit("Please install perf before running the script!")

# Ensure user has previllage to run perf
CHECK_PERF_EXECUTABILITY = subprocess.run(["perf", "stat", "ls", "/"],
                                          stdout=subprocess.DEVNULL,
                                          stderr=subprocess.DEVNULL,
                                          check=False)
if CHECK_PERF_EXECUTABILITY.returncode:
    sys.exit("""
Error:
You may not have permission to collect stats.
Consider tweaking /proc/sys/kernel/perf_event_paranoid,
which controls use of the performance events system by
unprivileged users (without CAP_SYS_ADMIN).
  -1: Allow use of (almost) all events by all users
      Ignore mlock limit after perf_event_mlock_kb without CAP_IPC_LOCK
   0: Disallow ftrace function tracepoint by users without CAP_SYS_ADMIN
      Disallow raw tracepoint access by users without CAP_SYS_ADMIN
   1: Disallow CPU event access by users without CAP_SYS_ADMIN
   2: Disallow kernel profiling by users without CAP_SYS_ADMIN
To make this setting permanent, edit /etc/sysctl.conf too, e.g.:
   kernel.perf_event_paranoid = -1

* Alternatively, you can run this script under sudo privileges.
""")

# Run perf and save all intermediate files in a temporary directory
with tempfile.TemporaryDirectory() as tmpdir:
    RECORD_PATH = os.path.join(tmpdir, "record.data")
    REPORT_PATH = os.path.join(tmpdir, "report.txt")

    PERF_RECORD = subprocess.run((["perf",
                                   "record",
                                   "-e",
                                   "instructions",
                                   "--output="+RECORD_PATH] +
                                  COMMAND),
                                 stdout=subprocess.DEVNULL,
                                 stderr=subprocess.PIPE,
                                 check=False)
    if PERF_RECORD.returncode:
        sys.exit(PERF_RECORD.stderr.decode("utf-8"))

    with open(REPORT_PATH, "w") as output:
        PERF_REPORT = subprocess.run(["perf",
                                      "report",
                                      "--input=" + RECORD_PATH,
                                      "--stdio"],
                                     stdout=output,
                                     stderr=subprocess.PIPE,
                                     check=False)
        if PERF_REPORT.returncode:
            sys.exit(PERF_REPORT.stderr.decode("utf-8"))

    # Save the reported data to FUNCTIONS[] and INSTRUCTIONS
    with open(REPORT_PATH, "r") as data:
        LINES = data.readlines()
        # Read the number of instructions
        INSTRUCTIONS = int(LINES[6].split()[-1])
        # Continue reading:
        # Only read lines that are not empty
        # Only read lines that are not comments (comments start with #)
        # Only read functions executed by qemu-system
        FUNCTIONS = [line for line in LINES if line
                     and line[0] != "\n"
                     and line[0] != "#"
                     and "qemu-system" in line.split()[2]]


# Limit the number of top functions to "TOP"
NO_TOP_FUNCTIONS = TOP if len(FUNCTIONS) > TOP else len(FUNCTIONS)

# Store the data of the top functions in TOP_FUNCTIONS[]
TOP_FUNCTIONS = FUNCTIONS[:NO_TOP_FUNCTIONS]

# Print total instructions
print("\nNumber of instructions:", format(INSTRUCTIONS, ","))
# Print table header
print("\n{:>4}  {:>10}  {}\n{}  {}  {}".format("No.",
                                               "Percentage",
                                               "Name",
                                               "-" * 4,
                                               "-" * 10,
                                               "-" * 30))

# Print top N functions
for (index, function) in enumerate(TOP_FUNCTIONS, start=1):
    function_data = function.split()
    function_percentage = function_data[0]
    function_name = function_data[-1]
    function_invoker = " ".join(function_data[2:-2])
    print("{:>4}  {:>10}  {}".format(index,
                                     function_percentage,
                                     function_name))
