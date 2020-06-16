#!/usr/bin/env python3

#  Print the top 25 most executed functions in QEMU using perf.
#  Example Usage:
#  perf_top_25.py <qemu-build>/x86_64-linux-user/qemu-x86_64 executable
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
(qemu_path, executable) = (sys.argv[1], ' '.join(sys.argv[2:]))

# Run perf repcord and report
os.system('sudo perf record {} {} 2> /dev/null \
            && sudo perf report --stdio > tmp.perf.data'
          .format(qemu_path, executable))

# Line number with the top function
first_func_line = 11

# Perf report output
perf_data = []

# Open perf report output and store it in perf_data
with open('tmp.perf.data', 'r') as data:
    perf_data = data.readlines()

# Number of functions recorded by perf
number_of_functions = len(perf_data) - first_func_line

# Limit the number of top functions to 25
number_of_top_functions = (25 if number_of_functions >
                           25 else number_of_functions)

# Store the data of the top functions in top_functions[]
top_functions = perf_data[first_func_line:first_func_line
                          + number_of_top_functions]

# Print information headers
print('{:>4}  {:>10}  {:<25}  {}\n{}  {}  {}  {}'.format('No.',
                                                         'Percentage',
                                                         'Name',
                                                         'Caller',
                                                         '-' * 4,
                                                         '-' * 10,
                                                         '-' * 25,
                                                         '-' * 25,
                                                         ))

# Print top 25 functions
for (index, function) in enumerate(top_functions, start=1):
    function_data = function.split()
    print('{:>4}  {:>10}  {:<25}  {}'.format(index,
                                             function_data[0],
                                             function_data[-1],
                                             ' '.join(function_data[2:-2])))

# Remove intermediate files
os.system('sudo rm perf.data tmp.perf.data')
