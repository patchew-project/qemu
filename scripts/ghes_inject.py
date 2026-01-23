#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Copyright (C) 2024-2025 Mauro Carvalho Chehab <mchehab+huawei@kernel.org>

"""
Handle ACPI GHESv2 error injection logic QEMU QMP interface.
"""

import argparse
import sys

from arm_processor_error import ArmProcessorEinj
from pci_bus_error import PciBusError
from fuzzy_error import FuzzyError
from raw_error import RawError

EINJ_DESC = """
Handles ACPI GHESv2 error injection via the QEMU QMP interface.

It uses UEFI BIOS APEI features to generate GHES records, which helps to
test CPER and GHES drivers on the guest OS and see how user‑space
applications on that guest handle such errors.
"""

def main():
    """Main program"""

    # Main parser - handle generic args like QEMU QMP TCP socket options
    parser = argparse.ArgumentParser(formatter_class=argparse.RawDescriptionHelpFormatter,
                                     usage="%(prog)s [options]",
                                     description=EINJ_DESC)

    g_options = parser.add_argument_group("QEMU QMP socket options")
    g_options.add_argument("-H", "--host", default="localhost", type=str,
                           help="host name (default: %(default)s)")
    g_options.add_argument("-P", "--port", default=4445, type=int,
                           help="TCP port number (default: %(default)s)")
    g_options.add_argument('-d', '--debug', action='store_true',
                           help="enable debug output (default: %(default)s)")

    subparsers = parser.add_subparsers()

    ArmProcessorEinj(subparsers)
    PciBusError(subparsers)
    FuzzyError(subparsers)
    RawError(subparsers)

    args = parser.parse_args()
    if "func" in args:
        if not args.func(args):
            sys.exit(1)
    else:
        print("Error: no command specified\n", file=sys.stderr)
        parser.print_help(file=sys.stderr)
        print(file=sys.stderr)
        sys.exit(f"Please specify a valid command for {sys.argv[0]}")

if __name__ == "__main__":
    main()
