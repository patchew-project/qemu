#!/usr/bin/env python3

"""
Print the callees of a given list of QEMU functions.

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


def find_function_lines(function_name: str,
                        callgrind_data: List[str]) -> List[int]:
    """
    Search for the line with the function name in the
    callgrind_annotate output when ran using --tre=calling.
    All the function callees should be listed after that line.

    Parameters:
    function_name (string): The desired function name to print its callees
    callgrind_data (List[str]): callgrind_annotate output

    Returns:
    (List[int]): List of function line numbers
    """
    lines = []
    for (i, callgrind_datum) in enumerate(callgrind_data):
        split_line = callgrind_datum.split()
        if len(split_line) > 2 and \
                split_line[1] == "*" and \
                split_line[2].split(":")[-1] == function_name:
            # Function might be in the callgrind_annotate output more than
            # once, so don't break after finding an instance
            if callgrind_data[i + 1] != "\n":
                # Only append the line number if the found instance has
                # callees
                lines.append(i)
    return lines


def get_function_calles(
        function_lines: List[int],
        callgrind_data: List[str]) -> List[List[Union[str, int]]]:
    """
    Get all callees data for a function given its list of line numbers in
    callgrind_annotate output.

    Parameters:
    function_lines (List[int]): Line numbers of the function to get its callees
    callgrind_data (List[str]): callgrind_annotate output

    Returns:
    (List[List[Union[str, int]]]):[[number_of_instructions(int),
                                    callee_name(str),
                                    number_of_calls(int),
                                    source_file(str)],
                                    ...]
    """
    callees: List[List[Union[str, int]]] = []
    for function_line in function_lines:
        next_callee = function_line + 1
        while callgrind_data[next_callee] != "\n":
            split_line = callgrind_data[next_callee].split()
            number_of_instructions = int(split_line[0].replace(",", ""))
            source_file = split_line[2].split(":")[0]
            callee_name = split_line[2].split(":")[1]
            number_of_calls = int(split_line[3][1:-2])
            callees.append([number_of_instructions, callee_name,
                            number_of_calls, source_file])
            next_callee += 1
    return sorted(callees, reverse=True)


def main():
    """
    Parse the command line arguments then start execution.

    Syntax:
    list_fn_callees.py [-h] -f FUNCTION [FUNCTION ...] -- \
               <qemu executable> [<qemu executable options>] \
               <target executable> [<target executable options>]

    [-h] - Print the script arguments help message.
    -f FUNCTION [FUNCTION ...] - List of function names

    Example of usage:
    list_fn_callees.py -f helper_float_sub_d helper_float_mul_d -- \
                      qemu-mips coulomb_double-mips
    """

    # Parse the command line arguments
    parser = argparse.ArgumentParser(
        usage="list_fn_callees.py [-h] -f FUNCTION [FUNCTION ...] -- "
        "<qemu executable> [<qemu executable options>] "
        "<target executable> [<target executable options>]")

    parser.add_argument("-f", dest="function", type=str,
                        nargs="+", required=True,
                        help="list of function names to print their callees")

    parser.add_argument("command", type=str, nargs="+", help=argparse.SUPPRESS)

    args = parser.parse_args()

    # Extract the needed variables from the args
    command = args.command
    function_names = args.function

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

        for function_name in function_names:
            # Line numbers with the desired function
            function_lines = find_function_lines(function_name, callgrind_data)

            if len(function_lines) == 0:
                print("Couldn't locate function: {}.\n".format(
                    function_name))
                continue

            # Get function callees
            function_callees = get_function_calles(
                function_lines, callgrind_data)

            print("Callees of {}:\n".format(function_name))

            # Print table header
            print("{:>4}  {:>15}  {:>10}  {:>15}  {:>10}  {:<25}  {}".
                  format(
                      "No.",
                      "Instructions",
                      "Percentage",
                      "Calls",
                      "Ins/Call",
                      "Function Name",
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

            for (index, callee) in enumerate(function_callees, start=1):
                instructions = callee[0]
                percentage = (callee[0] / total_instructions) * 100
                calls = callee[2]
                instruction_per_call = int(callee[0] / callee[2])
                function_name = callee[1]
                source_file = callee[3]
                # Print extracted data
                print("{:>4}  {:>15}  {:>9.3f}%  {:>15}  {:>10}  {:<25}  {}".
                      format(
                          index,
                          format(instructions, ","),
                          round(percentage, 3),
                          format(calls, ","),
                          format(instruction_per_call, ","),
                          function_name,
                          source_file)
                      )

            print("\n")


if __name__ == "__main__":
    main()
