#!/usr/bin/env python3

#  Print the top 25 most executed functions in QEMU using callgrind.
#  Example Usage:
#  callgrind_top_25.py <qemu-build>/x86_64-linux-user/qemu-x86_64 executable
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

import os
import sys

# Ensure sufficient arguments
if len(sys.argv) < 3:
    print('Insufficient Script Arguments!')
    sys.exit(1)

# Get the qemu path and the executable + its arguments
(qemu, executable) = (sys.argv[1], ' '.join(sys.argv[2:]))

# Run callgrind and callgrind_annotate
os.system('valgrind --tool=callgrind --callgrind-out-file=callgrind.data {} {} \
            2 > / dev / null & & callgrind_annotate callgrind.data \
            > tmp.callgrind.data'.
          format(qemu, executable))

# Line number with the total number of instructions
number_of_instructions_line = 20

# Line number with the top function
first_func_line = 25

# callgrind_annotate output
callgrind_data = []

# Open callgrind_annotate output and store it in callgrind_data
with open('tmp.callgrind.data', 'r') as data:
    callgrind_data = data.readlines()

# Get the total number of instructions
total_number_of_instructions = int(
    callgrind_data[number_of_instructions_line].split(' ')[0].replace(',', ''))

# Number of functions recorded by callgrind
number_of_functions = len(callgrind_data) - first_func_line

# Limit the number of top functions to 25
number_of_top_functions = (25 if number_of_functions >
                           25 else number_of_instructions_line)

# Store the data of the top functions in top_functions[]
top_functions = callgrind_data[first_func_line:
                               first_func_line + number_of_top_functions]
# Print information headers
print('{:>4}  {:>10}  {:<25}  {}\n{}  {}  {}  {}'.format('No.',
                                                         'Percentage',
                                                         'Name',
                                                         'Source File',
                                                         '-' * 4,
                                                         '-' * 10,
                                                         '-' * 25,
                                                         '-' * 30,
                                                         ))

# Print top 25 functions
for (index, function) in enumerate(top_functions, start=1):
    function_data = function.split()
    # Calculate function percentage
    percentage = (float(function_data[0].replace(
        ',', '')) / total_number_of_instructions) * 100
    # Get function source path and name
    path, name = function_data[1].split(':')
    # Print extracted data
    print('{:>4}  {:>9.3f}%  {:<25}  {}'.format(index,
                                                round(percentage, 3),
                                                name,
                                                path))

# Remove intermediate files
os.system('rm callgrind.data tmp.callgrind.data')
