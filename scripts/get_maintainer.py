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


#
# Helper functions for dealing with the source path
#

def is_qemu_src_root(directory):
    """
    Checks if the given path appears to be the root of a QEMU source tree,
    based on the presence of key files and directories.
    """
    if not path.isdir(directory):
        return False

    required_files = ['COPYING', 'MAINTAINERS', 'Makefile', 'VERSION']
    required_dirs = ['docs', 'linux-user', 'system']

    for f in required_files:
        if not path.isfile(path.join(directory, f)):
            return False
    for d in required_dirs:
        if not path.isdir(path.join(directory, d)):
            return False
    return True


def find_src_root():
    """
    Walks up the directory tree from the script's location
    to find the QEMU source root.
    Returns the absolute path of the root directory if found, or None.
    """
    script_dir = path.dirname(path.abspath(__file__))
    current_dir = script_dir

    while True:
        if is_qemu_src_root(current_dir):
            return current_dir

        # Move up to the parent directory
        parent_dir = path.dirname(current_dir)

        # If we reached the filesystem root and haven't found it, stop
        if parent_dir == current_dir:
            break

        current_dir = parent_dir

    return None

#
# Argument validation
#


def valid_file_path(arg):
    """
    Checks if arg exists and is a regular file or raises ArgumentTypeError.
    """
    if not path.exists(arg):
        raise ArgumentTypeError(f"File '{arg}' does not exist.")
    if not path.isfile(arg):
        raise ArgumentTypeError(f"Path '{arg}' is not a regular file.")
    return Path(path.abspath(arg))


def valid_src_root(arg):
    """
    Checks if arg is a valid QEMU source root or raise ArgumentTypeError.
    """
    abs_path = path.abspath(arg)
    if not is_qemu_src_root(abs_path):
        raise ArgumentTypeError(f"Path '{arg}' is not a valid QEMU source tree")
    return abs_path


def main():
    """
    Main entry point for the script.
    """

    parser = ArgumentParser(description='Extract maintainer information. ')

    # We can either specify patches or an individual file
    group = parser.add_mutually_exclusive_group()
    group.add_argument('patch', nargs='*', type=valid_file_path,
                       help='path to patch file')
    group.add_argument('-f', '--file', type=valid_file_path,
                       help='path to source file')

    # We need to know or be told where the root of the source tree is
    src = find_src_root()

    if src is None:
        parser.add_argument('--src', type=valid_src_root, required=True,
                            help='Root of QEMU source tree')
    else:
        parser.add_argument('--src', type=valid_src_root, default=src,
                            help=f'Root of QEMU source tree (default: {src})')

    args = parser.parse_args()



if __name__ == '__main__':
    main()
