# Copyright (C) 2020 Red Hat, Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import os
import re
import subprocess
import sys
from typing import List


# TODO: Empty this list!
SKIP_FILES = (
    '030', '040', '041', '044', '045', '055', '056', '057', '065', '093',
    '096', '118', '124', '132', '136', '139', '147', '148', '149',
    '151', '152', '155', '163', '165', '194', '196', '202',
    '203', '205', '206', '207', '208', '210', '211', '212', '213', '216',
    '218', '219', '224', '228', '234', '235', '236', '237', '238',
    '240', '242', '245', '246', '248', '255', '256', '257', '258', '260',
    '262', '264', '266', '274', '277', '280', '281', '295', '296', '298',
    '299', '302', '303', '304', '307',
    'nbd-fault-injector.py', 'qcow2.py', 'qcow2_format.py', 'qed.py'
)


def is_python_file(filename):
    if not os.path.isfile(filename):
        return False

    if filename.endswith('.py'):
        return True

    with open(filename, encoding='utf-8') as f:
        try:
            first_line = f.readline()
            return re.match('^#!.*python', first_line) is not None
        except UnicodeDecodeError:  # Ignore binary files
            return False


def get_test_files() -> List[str]:
    named_tests = [f'tests/{entry}' for entry in os.listdir('tests')]
    check_tests = set(os.listdir('.') + named_tests) - set(SKIP_FILES)
    return list(filter(is_python_file, check_tests))


def run_linter(tool: str, args: List[str]) -> int:
    """
    Run a python-based linting tool.
    """
    p = subprocess.run(
        ('python3', '-m', tool, *args),
        check=False,
    )

    return p.returncode


def main() -> int:
    """
    Used by the Python CI system as an entry point to run these linters.
    """
    files = get_test_files()

    if sys.argv[1] == '--pylint':
        return run_linter('pylint', files)
    elif sys.argv[1] == '--mypy':
        # mypy bug #9852; disable incremental checking as a workaround.
        args = ['--no-incremental'] + files
        return run_linter('mypy', args)

    raise ValueError(f"Unrecognized argument(s): '{sys.argv[1:]}'")


if __name__ == '__main__':
    sys.exit(main())
