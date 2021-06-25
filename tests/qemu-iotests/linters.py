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
from typing import List, Mapping, Optional


# TODO: Empty this list!
SKIP_FILES = (
    '030', '040', '041', '044', '045', '055', '056', '057', '065', '093',
    '096', '118', '124', '132', '136', '139', '147', '148', '149',
    '151', '152', '155', '163', '165', '169', '194', '196', '199', '202',
    '203', '205', '206', '207', '208', '210', '211', '212', '213', '216',
    '218', '219', '222', '224', '228', '234', '235', '236', '237', '238',
    '240', '242', '245', '246', '248', '255', '256', '257', '258', '260',
    '262', '264', '266', '274', '277', '280', '281', '295', '296', '298',
    '299', '302', '303', '304', '307',
    'nbd-fault-injector.py', 'qcow2.py', 'qcow2_format.py', 'qed.py'
)


def is_python_file(filename: str, directory: str = '.') -> bool:
    filepath = os.path.join(directory, filename)

    if not os.path.isfile(filepath):
        return False

    if filename.endswith('.py'):
        return True

    with open(filepath) as f:
        try:
            first_line = f.readline()
            return re.match('^#!.*python', first_line) is not None
        except UnicodeDecodeError:  # Ignore binary files
            return False


def get_test_files(directory: str = '.') -> List[str]:
    return [
        f for f in (set(os.listdir(directory)) - set(SKIP_FILES))
        if is_python_file(f, directory)
    ]


def run_linters(
    files: List[str],
    directory: str = '.',
    env: Optional[Mapping[str, str]] = None,
) -> int:
    ret = 0

    print('=== pylint ===')
    sys.stdout.flush()

    # Todo notes are fine, but fixme's or xxx's should probably just be
    # fixed (in tests, at least)
    p = subprocess.run(
        ('python3', '-m', 'pylint', '--score=n', '--notes=FIXME,XXX', *files),
        cwd=directory,
        env=env,
        check=False,
        universal_newlines=True,
    )
    ret += p.returncode

    print('=== mypy ===')
    sys.stdout.flush()

    # We have to call mypy separately for each file.  Otherwise, it
    # will interpret all given files as belonging together (i.e., they
    # may not both define the same classes, etc.; most notably, they
    # must not both define the __main__ module).
    for filename in files:
        p = subprocess.run(
            (
                'python3', '-m', 'mypy',
                '--warn-unused-configs',
                '--disallow-subclassing-any',
                '--disallow-any-generics',
                '--disallow-incomplete-defs',
                '--disallow-untyped-decorators',
                '--no-implicit-optional',
                '--warn-redundant-casts',
                '--warn-unused-ignores',
                '--no-implicit-reexport',
                '--namespace-packages',
                filename,
            ),
            cwd=directory,
            env=env,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            universal_newlines=True
        )

        ret += p.returncode
        if p.returncode != 0:
            print(p.stdout)

    return ret
