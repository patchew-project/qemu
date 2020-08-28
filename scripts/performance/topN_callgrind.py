#!/usr/bin/env python3

"""
Print the top N most executed functions in QEMU using callgrind.

Syntax:
topN_callgrind.py [-h] [-n <number of displayed top functions>] -- \
         <qemu executable> [<qemu executable options>] \
         <target executable> [<target execurable options>]

[-h] - Print the script arguments help message.
[-n] - Specify the number of top functions to print.
     - If this flag is not specified, the tool defaults to 25.

Example of usage:
topN_callgrind.py -n 20 -- qemu-arm coulomb_double-arm

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
    usage='topN_callgrind.py [-h] [-n] <number of displayed top functions> -- '
          '<qemu executable> [<qemu executable options>] '
          '<target executable> [<target executable options>]')

PARSER.add_argument('-n', dest='top', type=int, default=25,
                    help='Specify the number of top functions to print.')

PARSER.add_argument('command', type=str, nargs='+', help=argparse.SUPPRESS)

ARGS = PARSER.parse_args()

# Extract the needed variables from the args
COMMAND = ARGS.command
TOP = ARGS.top

# Insure that valgrind is installed
CHECK_VALGRIND_PRESENCE = subprocess.run(["which", "valgrind"],
                                         stdout=subprocess.DEVNULL,
                                         check=False)
if CHECK_VALGRIND_PRESENCE.returncode:
    sys.exit("Please install valgrind before running the script!")

# Run callgrind and save all intermediate files in a temporary directory
with tempfile.TemporaryDirectory() as tmpdir:
    CALLGRIND_DATA_PATH = os.path.join(tmpdir, "callgrind.data")
    ANNOTATE_OUT_PATH = os.path.join(tmpdir, "callgrind_annotate.out")

    # Run callgrind
    CALLGRIND = subprocess.run((["valgrind",
                                 "--tool=callgrind",
                                 "--callgrind-out-file="+CALLGRIND_DATA_PATH]
                                + COMMAND),
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.PIPE,
                               check=False)
    if CALLGRIND.returncode:
        sys.exit(CALLGRIND.stderr.decode("utf-8"))

    with open(ANNOTATE_OUT_PATH, "w") as output:
        CALLGRIND_ANNOTATE = subprocess.run(["callgrind_annotate",
                                             CALLGRIND_DATA_PATH],
                                            stdout=output,
                                            stderr=subprocess.PIPE,
                                            check=False)
        if CALLGRIND_ANNOTATE.returncode:
            sys.exit(CALLGRIND_ANNOTATE.stderr.decode("utf-8"))

    # Read the callgrind_annotate output to CALLGRIND_DATA[]
    CALLGRIND_DATA = []
    with open(ANNOTATE_OUT_PATH, 'r') as data:
        CALLGRIND_DATA = data.readlines()

# Line number with the total number of instructions
TOTAL_INSTRUCTIONS_LINE_NO = 20

# Get the total number of instructions
TOTAL_INSTRUCTIONS_LINE_DATA = CALLGRIND_DATA[TOTAL_INSTRUCTIONS_LINE_NO]
TOTAL_NUMBER_OF_INSTRUCTIONS = TOTAL_INSTRUCTIONS_LINE_DATA.split(' ')[0]
TOTAL_NUMBER_OF_INSTRUCTIONS = int(
    TOTAL_NUMBER_OF_INSTRUCTIONS.replace(',', ''))

# Line number with the top function
FIRST_FUNC_LINE = 25

# Number of functions recorded by callgrind, last two lines are always empty
NUMBER_OF_FUNCTIONS = len(CALLGRIND_DATA) - FIRST_FUNC_LINE - 2

# Limit the number of top functions to "top"
NUMBER_OF_TOP_FUNCTIONS = (TOP if NUMBER_OF_FUNCTIONS >
                           TOP else NUMBER_OF_FUNCTIONS)

# Store the data of the top functions in top_functions[]
TOP_FUNCTIONS = CALLGRIND_DATA[FIRST_FUNC_LINE:
                               FIRST_FUNC_LINE + NUMBER_OF_TOP_FUNCTIONS]

# Print table header
print('{:>4}  {:>10}  {:<30}  {}\n{}  {}  {}  {}'.format('No.',
                                                         'Percentage',
                                                         'Function Name',
                                                         'Source File',
                                                         '-' * 4,
                                                         '-' * 10,
                                                         '-' * 30,
                                                         '-' * 30,
                                                         ))

# Print top N functions
for (index, function) in enumerate(TOP_FUNCTIONS, start=1):
    function_data = function.split()
    # Calculate function percentage
    function_instructions = float(function_data[0].replace(',', ''))
    function_percentage = (function_instructions /
                           TOTAL_NUMBER_OF_INSTRUCTIONS)*100
    # Get function name and source files path
    function_source_file, function_name = function_data[1].split(':')
    # Print extracted data
    print('{:>4}  {:>9.3f}%  {:<30}  {}'.format(index,
                                                round(function_percentage, 3),
                                                function_name,
                                                function_source_file))
