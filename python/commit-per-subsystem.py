#!/usr/bin/env python3
#
# Copyright (c) 2019 Virtuozzo International GmbH
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
#

import subprocess
import sys


def git_add(pattern):
    subprocess.run(['git', 'add', pattern])


def git_commit(msg):
    subprocess.run(['git', 'commit', '-m', msg], capture_output=True)


maintainers = sys.argv[1]
message = sys.argv[2].strip()

subsystem = None

shortnames = {
    'Block layer core': 'block',
    'ARM cores': 'arm',
    'Network Block Device (NBD)': 'nbd',
    'Command line option argument parsing': 'cmdline',
    'Character device backends': 'chardev',
    'S390 general architecture support': 's390'
}


def commit():
    if subsystem:
        msg = subsystem
        if msg in shortnames:
            msg = shortnames[msg]
        msg += ': ' + message
        git_commit(msg)


with open(maintainers) as f:
    for line in f:
        line = line.rstrip()
        if not line:
            continue
        if len(line) >= 2 and line[1] == ':':
            if line[0] == 'F' and line[3:] not in ['*', '*/']:
                git_add(line[3:])
        else:
            # new subsystem start
            commit()

            subsystem = line

commit()
