#!/usr/bin/env python
#
# get_maintainer.py: Print selected MAINTAINERS information
#
# usage: scripts/get_maintainer.py [OPTIONS] <patch>
#        scripts/get_maintainer.py [OPTIONS] -f <file>
#
# (c) 2025, Alex Bennée <alex.bennee@linaro.org>
#           based on get_maintainers.pl
#
# SPDX-License-Identifier: GPL-2.0-or-later

from argparse import ArgumentParser, ArgumentTypeError
from os import path
from pathlib import Path


def valid_file_path(arg):
    """
    Checks if arg exists and is a regular file or raises ArgumentTypeError.
    """
    if not path.exists(arg):
        raise ArgumentTypeError(f"File '{arg}' does not exist.")
    if not path.isfile(arg):
        raise ArgumentTypeError(f"Path '{arg}' is not a regular file.")
    return Path(path.abspath(arg))


def main():
    parser = ArgumentParser(description='Extract maintainer information. ')

    # We can either specify patches or an individual file
    group = parser.add_mutually_exclusive_group()
    group.add_argument('patch', nargs='*', type=valid_file_path,
                       help='path to patch file')
    group.add_argument('-f', '--file', type=valid_file_path,
                       help='path to source file')

    args = parser.parse_args()



if __name__ == '__main__':
    main()
