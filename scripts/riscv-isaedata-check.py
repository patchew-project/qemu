#!/usr/bin/env python3
# Copyright (c) 2025 Ventana Micro Systems Inc.
# SPDX-License-Identifier: GPL-2.0-or-later

#
# Check if isa_edata_arr[] from target/riscv/cpu.c is
# ordered according to the RISC-V specification.
#

import os

"""
This script aims to check the ordering of isa_edata_arr[] array
from target/riscv/cpu.c.

The RISC-V riscv,isa ordering has a lot of rules (see the comment
right before isa_edata_arr[] in target/riscv/cpu.c) and we're not
able to keep up with it during the review process. In the end
every new extension added has a good chance of breaking it.

The idea with this script is to try to catch these errors earlier
by directly reading isa_edata_arr[] and pointing out discrepancies
found. E.g.:

$ python3 riscv-isaedata-check.py
Wrong ordering: sspm must succeed ssctr

This indicates that 'ssctr' must be put earlier that sspm in the
array.

A successful run of this script has a retval = 0 and no output.
"""

def ext_is_sorted(ext1: str, ext2: str) -> bool:
    """Check if the RISC-V ISA extension 'ext1' comes after
       the RISC-V ISA extension 'ext2' using the RISC-V sorting
       rules. We're summing up these rules in 3 steps when
       comparing isa_edata extensions:

        1. If both extensions does not start with 'z', they're
           sorted with regular alphabetical order;

        2. A 'z' extension always precedes a non 'z' extension;

        3. If both extensions starts with 'z', check the second
           letter of both:
             - if they're the same, sort it in alphabetical order;
             - otherwise, sort it via the Z extension category
               (IMAFDQLCBKJTPVH).

       Args:
            ext1 (str): lower-case RISC-V isa extension name
            ext2 (str): lower-case RISC-V isa extension name

       Returns:
            bool: True ext1 precedes ext2, False otherwise.
    """
    z_order = ['i','m','a','f','d','q', 'l', 'c', 'b',
               'k', 'j', 't', 'p', 'v', 'h']
    order1 = len(z_order)
    order2 = len(z_order)

    if ext1[0] != 'z' and ext2[0] != 'z':
        return ext1 < ext2

    if ext1[0] == 'z' and ext2[0] != 'z':
        return True

    if ext1[0] != 'z' and ext2[0] == 'z':
        return False

    # At this point we know both starts with 'z'. If
    # they're both the same z_order use alphabetical
    # order.
    if ext1[1] == ext2[1]:
        return ext1 < ext2

    # Get the order within the z category for each
    for i in range(len(z_order)):
        if ext1[1] == z_order[i]:
            order1 = i;
        if ext2[1] == z_order[i]:
            order2 = i;
        if order1 != len(z_order) and order2 != len(z_order):
            break

    if order1 < order2:
        return True

    return False
# end ext_is_sorted


def get_extension_name(line: str) -> str:
    """Given a 'line' str in the format

       ISA_EXT_DATA_ENTRY(ext_name, ...)

       Return 'ext_name' if successful or None otherwise.
    """
    match_str = "ISA_EXT_DATA_ENTRY("
    match_idx = line.rfind(match_str)

    if match_idx < 0:
        return None

    last_idx = line.find(",")
    if last_idx < 0:
        return None

    match_idx += len(match_str)

    return line[match_idx:last_idx]
#end get_extension_name


def main():
    dir_path = os.path.dirname(os.path.realpath(__file__))
    filename = dir_path + "/../target/riscv/cpu.c"

    try:
        with open(filename, 'r') as file:
            isaEdataFound = False
            ext1 = None
            ext2 = None

            for line in file:
                if "const RISCVIsaExtData isa_edata_arr[]" in line:
                    isaEdataFound = True
                    continue

                if not isaEdataFound:
                    continue

                if "{ }" in line:
                    break

                tmp = get_extension_name(line)

                if tmp is None:
                    continue

                if ext1 is None:
                    ext1 = tmp
                    continue

                if ext2 is None:
                    ext2 = tmp
                else:
                    ext1 = ext2
                    ext2 = tmp

                if not ext_is_sorted(ext1, ext2):
                    print(f"Wrong ordering: {ext1} must succeed {ext2}")
                    exit(1)
    except FileNotFoundError:
        print(f"Error: The file '{filename}' was not found.")
    except Exception as e:
        print(f"An error occurred: {e}")
# end main


if __name__ == '__main__':
    main()
