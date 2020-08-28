#!/usr/bin/env python3

"""
Print the executed helpers of a QEMU invocation.

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

from typing import List, Union


def find_jit_line(callgrind_data: List[str]) -> int:
    """
    Search for the line with the JIT call in the callgrind_annotate
    output when ran using --tre=calling.
    All the helpers should be listed after that line.

    Parameters:
    callgrind_data (List[str]): callgrind_annotate output

    Returns:
    (int): Line number of JIT call
    """
    line = -1
    for (i, callgrind_datum) in enumerate(callgrind_data):
        split_line = callgrind_datum.split()
        if len(split_line) > 2 and \
                split_line[1] == "*" and \
                split_line[-1] == "[???]":
            line = i
            break
    return line


def get_helpers(jit_line: int,
                callgrind_data: List[str]) -> List[List[Union[str, int]]]:
    """
    Get all helpers data given the line number of the JIT call.

    Parameters:
    jit_line (int): Line number of the JIT call
    callgrind_data (List[str]): callgrind_annotate output

    Returns:
    (List[List[Union[str, int]]]):[[number_of_instructions(int),
                                    helper_name(str),
                                    number_of_calls(int),
                                    source_file(str)],
                                    ...]
    """
    helpers: List[List[Union[str, int]]] = []
    next_helper = jit_line + 1
    while callgrind_data[next_helper] != "\n":
        split_line = callgrind_data[next_helper].split()
        number_of_instructions = int(split_line[0].replace(",", ""))
        source_file = split_line[2].split(":")[0]
        callee_name = split_line[2].split(":")[1]
        number_of_calls = int(split_line[3][1:-2])
        helpers.append([number_of_instructions, callee_name,
                        number_of_calls, source_file])
        next_helper += 1
    return sorted(helpers, reverse=True)


def main():
    """
    Parse the command line arguments then start execution

    Syntax:
    list_helpers.py [-h] -- \
               <qemu executable> [<qemu executable options>] \
               <target executable> [<target executable options>]

    [-h] - Print the script arguments help message.

    Example of usage:
    list_helpers.py -- qemu-mips coulomb_double-mips
    """
    # Parse the command line arguments
    parser = argparse.ArgumentParser(
        usage="list_helpers.py [-h] -- "
        "<qemu executable> [<qemu executable options>] "
        "<target executable> [<target executable options>]")

    parser.add_argument("command", type=str, nargs="+", help=argparse.SUPPRESS)

    args = parser.parse_args()

    # Extract the needed variables from the args
    command = args.command

    # Insure that valgrind is installed
    check_valgrind = subprocess.run(
        ["which", "valgrind"], stdout=subprocess.DEVNULL, check=False)
    if check_valgrind.returncode:
        sys.exit("Please install valgrind before running the script.")

    # Save all intermediate files in a temporary directory
    with tempfile.TemporaryDirectory() as tmpdirname:
        # callgrind output file path
        data_path = os.path.join(tmpdirname, "callgrind.data")
        # callgrind_annotate output file path
        annotate_out_path = os.path.join(tmpdirname, "callgrind_annotate.out")

        # Run callgrind
        callgrind = subprocess.run((["valgrind",
                                     "--tool=callgrind",
                                     "--callgrind-out-file=" + data_path]
                                    + command),
                                   stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE,
                                   check=False)
        if callgrind.returncode:
            sys.exit(callgrind.stderr.decode("utf-8"))

        # Save callgrind_annotate output
        with open(annotate_out_path, "w") as output:
            callgrind_annotate = subprocess.run(["callgrind_annotate",
                                                 data_path,
                                                 "--threshold=100",
                                                 "--tree=calling"],
                                                stdout=output,
                                                stderr=subprocess.PIPE,
                                                check=False)
            if callgrind_annotate.returncode:
                sys.exit(callgrind_annotate.stderr.decode("utf-8"))

        # Read the callgrind_annotate output to callgrind_data[]
        callgrind_data = []
        with open(annotate_out_path, "r") as data:
            callgrind_data = data.readlines()

        # Line number with the total number of instructions
        total_instructions_line_number = 20
        # Get the total number of instructions
        total_instructions_line_data = \
            callgrind_data[total_instructions_line_number]
        total_instructions = total_instructions_line_data.split()[0]

        print("Total number of instructions: {}\n".format(total_instructions))

        # Remove commas and convert to int
        total_instructions = int(total_instructions.replace(",", ""))

        # Line number with the JIT call
        jit_line = find_jit_line(callgrind_data)
        if jit_line == -1:
            sys.exit("Couldn't locate the JIT call ... Exiting")

        # Get helpers
        helpers = get_helpers(jit_line, callgrind_data)

        print("Executed QEMU Helpers:\n")

        # Print table header
        print("{:>4}  {:>15}  {:>10}  {:>15}  {:>10}  {:<25}  {}".
              format(
                  "No.",
                  "Instructions",
                  "Percentage",
                  "Calls",
                  "Ins/Call",
                  "Helper Name",
                  "Source File")
              )

        print("{:>4}  {:>15}  {:>10}  {:>15}  {:>10}  {:<25}  {}".
              format(
                  "-" * 4,
                  "-" * 15,
                  "-" * 10,
                  "-" * 15,
                  "-" * 10,
                  "-" * 25,
                  "-" * 30)
              )

        for (index, callee) in enumerate(helpers, start=1):
            instructions = callee[0]
            percentage = (callee[0] / total_instructions) * 100
            calls = callee[2]
            instruction_per_call = int(callee[0] / callee[2])
            helper_name = callee[1]
            source_file = callee[3]
            # Print extracted data
            print("{:>4}  {:>15}  {:>9.3f}%  {:>15}  {:>10}  {:<25}  {}".
                  format(
                      index,
                      format(instructions, ","),
                      round(percentage, 3),
                      format(calls, ","),
                      format(instruction_per_call, ","),
                      helper_name,
                      source_file)
                  )


if __name__ == "__main__":
    main()
