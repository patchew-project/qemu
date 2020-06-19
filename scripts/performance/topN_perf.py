#!/usr/bin/env python3

#  Print the top N most executed functions in QEMU using perf.
#  Example Usage:
#  topN_perf.py -n 20 -- /path/to/qemu program -program -flags
#
#   If '-n' is not specified, the default is 25.
#
#  This file is a part of the project "TCG Continuous Benchmarking".
#
#  Copyright (C) 2020  Ahmed Karaman <ahmedkhaledkaraman@gmail.com>
#  Copyright (C) 2020  Aleksandar Markovic <aleksandar.qemu.devel@gmail.com>
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program. If not, see <https://www.gnu.org/licenses/>.

import argparse
import os
import subprocess
import sys


# Parse the command line arguments
parser = argparse.ArgumentParser(usage='topN_perf.py [-h] [-n TOP_FUNCTIONS] --'
                                ' /path/to/qemu program -[flags PROGRAM_FLAGS]')

parser.add_argument('-n', dest='top', type=int, default=25,
                    help='Specify the number of top functions to print.')

parser.add_argument('command', type=str, nargs='+', help=argparse.SUPPRESS)

args = parser.parse_args()

# Extract the needed variables from the args
command = args.command
top = args.top

# Insure that perf is installed
check_perf = subprocess.run(["which", "perf"], stdout=subprocess.DEVNULL)
if check_perf.returncode:
    sys.exit("Please install perf before running the script!")

# Insure user has previllage to run perf
check_previlage = subprocess.run(["perf", "stat", "ls", "/"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
if check_previlage.returncode:
    sys.exit(check_previlage.stderr.decode("utf-8") +
             "\nOr alternatively, you can run the script with sudo privileges!")

# Run perf record
perf_record = subprocess.run((["perf", "record"] + command),
                             stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
if perf_record.returncode:
    os.unlink('perf.data')
    sys.exit(perf_record.stderr.decode("utf-8"))

# Save perf report output to tmp.perf.data
with open("tmp.perf.data", "w") as output:
    perf_report = subprocess.run(
        ["perf", "report", "--stdio"], stdout=output, stderr=subprocess.PIPE)
    if perf_report.returncode:
        os.unlink('perf.data')
        output.close()
        os.unlink('tmp.perf.data')
        sys.exit(perf_report.stderr.decode("utf-8"))

# Read the reported data to functions[]
functions = []
with open("tmp.perf.data", "r") as data:
    # Only read lines that are not comments (comments start with #)
    # Only read lines that are not empty
    functions = [line for line in data.readlines() if line and line[0]
                 != '#' and line[0] != "\n"]

# Limit the number of top functions to "top"
number_of_top_functions = top if len(functions) > top else len(functions)

# Store the data of the top functions in top_functions[]
top_functions = functions[:number_of_top_functions]

# Print information headers
print('{:>4}  {:>10}  {:<30}  {}\n{}  {}  {}  {}'.format('No.',
                                                         'Percentage',
                                                         'Name',
                                                         'Caller',
                                                         '-' * 4,
                                                         '-' * 10,
                                                         '-' * 30,
                                                         '-' * 25))


# Print top N functions
for (index, function) in enumerate(top_functions, start=1):
    function_data = function.split()
    function_percentage = function_data[0]
    function_name = function_data[-1]
    function_caller = ' '.join(function_data[2:-2])
    print('{:>4}  {:>10}  {:<30}  {}'.format(index,
                                             function_percentage,
                                             function_name,
                                             function_caller))

# Remove intermediate files
os.unlink('perf.data')
os.unlink('tmp.perf.data')
