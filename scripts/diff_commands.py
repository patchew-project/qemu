#!/usr/bin/env python3
# Copyright (c) 2020 Red Hat, Inc.
#
# Author:
#  Marc-André Lureau <marcandre.lureau@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import argparse
import difflib
import subprocess
import sys


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("EXE1")
    parser.add_argument("EXE2")
    args = parser.parse_args()

    exe1_out = subprocess.check_output(args.EXE1, universal_newlines=True)
    exe2_out = subprocess.check_output(args.EXE2, universal_newlines=True)
    out_diff = difflib.unified_diff(
        exe1_out.splitlines(True),
        exe2_out.splitlines(True),
        fromfile=args.EXE1,
        tofile=args.EXE2,
    )
    has_diff = False
    for line in out_diff:
        has_diff = True
        sys.stdout.write(line)

    if has_diff:
        sys.exit(1)


if __name__ == "__main__":
    main()
