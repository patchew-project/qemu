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

from argparse import ArgumentParser, ArgumentTypeError, BooleanOptionalAction
from os import path
from pathlib import Path
from enum import StrEnum, auto
from re import compile as re_compile
from re import sub as re_sub

#
# Subsystem MAINTAINER entries
#
# The MAINTAINERS file is an unstructured text file where the
# important information is in lines that follow the form:
#
# X: some data
#
# where X is a documented tag and the data is variously an email,
# path, regex or link. Other lines should be ignored except the
# preceding non-blank or underlined line which represents the name of
# the "subsystem" or general area of the project.
#
# A blank line denominates the end of a section.
#

tag_re = re_compile(r"^([A-Z]):")


class UnhandledTag(Exception):
    "Exception for unhandled tags"


class BadStatus(Exception):
    "Exception for unknown status"


class Status(StrEnum):
    "Maintenance status"

    UNKNOWN = auto()
    SUPPORTED = 'Supported'
    MAINTAINED = 'Maintained'
    ODD_FIXES = 'Odd Fixes'
    ORPHAN = 'Orphan'
    OBSOLETE = 'Obsolete'

    @classmethod
    def _missing_(cls, value):
        # _missing_ is only invoked by the enum machinery if 'value' does not
        # match any existing enum member's value.
        # So, if we reach this point, 'value' is inherently invalid for this enum.
        raise BadStatus(f"'{value}' is not a valid maintenance status.")


person_re = re_compile(r"^(?P<name>[^<]+?)\s*<(?P<email>[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,})>\s*(?:@(?P<handle>\w+))?$")


class BadPerson(Exception):
    "Exception for un-parsable person"


class Person:
    "Class representing a maintainer or reviewer and their details"

    def __init__(self, info):
        match = person_re.search(info)

        if match is None:
            raise BadPerson(f"Failed to parse {info}")

        self.name = match.group('name')
        self.email = match.group('email')


class MaintainerSection:
    "Class representing a section of MAINTAINERS"

    def _expand(self, pattern):
        if pattern.endswith("/"):
            return f"{pattern}*"
        return pattern

    def __init__(self, section, entries):
        self.section = section
        self.status = Status.UNKNOWN
        self.maintainers = []
        self.reviewers = []
        self.files = []
        self.files_exclude = []
        self.trees = []
        self.lists = []
        self.web = []
        self.keywords = []

        for e in entries:
            (tag, data) = e.split(": ", 2)

            if tag == "M":
                person = Person(data)
                self.maintainers.append(person)
            elif tag == "R":
                person = Person(data)
                self.reviewers.append(person)
            elif tag == "S":
                self.status = Status(data)
            elif tag == "L":
                self.lists.append(data)
            elif tag == 'F':
                pat = self._expand(data)
                self.files.append(pat)
            elif tag == 'W':
                self.web.append(data)
            elif tag == 'K':
                self.keywords.append(data)
            elif tag == 'T':
                self.trees.append(data)
            elif tag == 'X':
                pat = self._expand(data)
                self.files_exclude.append(pat)
            else:
                raise UnhandledTag(f"'{tag}' is not understood.")

    def __str__(self) -> str:
        entries = []

        for m in self.maintainers:
            entries.append(f"{m.name} <{m.email}> (maintainer: {self.section})")

        for r in self.reviewers:
            entries.append(f"{r.name} <{r.email}> (reviewer: {self.section})")

        for l in self.lists:
            entries.append(f"{l} (open list: {self.section})")

        return "\n".join(entries)

    def is_file_covered(self, filename):
        "Is filename covered by this maintainer section"

        for fx in self.files_exclude:
            if filename.match(fx):
                return False

        for f in self.files:
            if filename.match(f):
                return True

        return False


def read_maintainers(src):
    """
    Read the MAINTAINERS file, return a list of MaintainerSection objects.
    """

    mfile = path.join(src, 'MAINTAINERS')
    entries = []

    section = None
    fields = []

    with open(mfile, 'r', encoding='utf-8') as f:
        for line in f:
            if not line.strip():  # Blank line found, potential end of a section
                if section:
                    new_section = MaintainerSection(section, fields)
                    entries.append(new_section)
                    # reset for next section
                    section = None
                    fields = []
            elif tag_re.match(line):
                fields.append(line.strip())
            else:
                if line.startswith("-") or line.startswith("="):
                    continue

                section = line.strip()

    return entries

#
# Helper functions for dealing with patch files
#
patchfile_re = re_compile(r"\+\+\+\s+(\S+)")

#TODO: also return a list of keyword hits for K:?
def extract_filenames_from_patch(patchfile):
    """
    Read a patchfile and return a list of files which are modified by
    the patch.
    """
    file_list = []

    with open(patchfile, 'r', encoding='utf-8') as f:
        for line in f:
            m = patchfile_re.match(line)
            if m:
                # strip leading [ab]/
                stripped = re_sub(r'^[^/]*/', '', m.group(1))
                as_path = Path(path.abspath(stripped))
                file_list.append(as_path)

    return file_list

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

    # Validate MAINTAINERS
    parser.add_argument('--validate',
                        action=BooleanOptionalAction,
                        default=None,
                        help="Just validate MAINTAINERS file")

    # We need to know or be told where the root of the source tree is
    src = find_src_root()

    if src is None:
        parser.add_argument('--src', type=valid_src_root, required=True,
                            help='Root of QEMU source tree')
    else:
        parser.add_argument('--src', type=valid_src_root, default=src,
                            help=f'Root of QEMU source tree (default: {src})')

    args = parser.parse_args()

    try:
        # Now we start by reading the MAINTAINERS file
        maint_sections = read_maintainers(args.src)
    except Exception as e:
        print(f"Error: {e}")
        exit(-1)

    if args.validate:
        print(f"loaded {len(maint_sections)} from MAINTAINERS")
        exit(0)

    # Build array of Path objects representing the files
    files = []

    if args.file:
        files.append(args.file)

    if args.patch:
        for p in args.patch:
            pfiles = extract_filenames_from_patch(p)
            files.extend(pfiles)

    # unique set of maintainer sections
    maintained: set[MaintainerSection] = set()

    for f in files:
        fmaint = [ms for ms in maint_sections if ms.is_file_covered(f)]
        for m in fmaint:
            maintained.add(m)

    for rm in maintained:
        print(str(rm))


if __name__ == '__main__':
    main()
