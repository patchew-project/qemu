#! /usr/bin/env python3

# Check incorrect file entries in MAINTAINERS
#
# Author: Pierrick Bouvier <pierrick.bouvier@oss.qualcomm.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import glob
import sys


def check_one_entry(line) -> bool:
    return True


def main() -> None:
    parser = argparse.ArgumentParser(description="Check MAINTAINERS file")
    parser.add_argument("maintainers", help="Path to MAINTAINERS file")
    args = parser.parse_args()

    found_file_entry = False
    found_incorrect_entries = False
    line_counter = 0

    with open(args.maintainers) as file:
        for entry in file:
            line_counter += 1

            if not entry.startswith("F:"):
                continue
            entry = entry[2:].strip()
            found_file_entry = True

            file_exists = len(glob.glob(entry, recursive=True)) > 0
            if file_exists:
                continue

            found_incorrect_entries = True
            print(
                f"No matching files for {args.maintainers} +{line_counter}: {entry}",
                file=sys.stderr,
            )

    if not found_file_entry:
        raise Exception("no file entry found - is MAINTAINERS path correct?")
    if found_incorrect_entries:
        raise Exception(f"incorrect entries found in {args.maintainers}")


if __name__ == "__main__":
    main()
