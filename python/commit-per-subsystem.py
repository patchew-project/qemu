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
import os
import glob


def git_add(pattern):
    subprocess.run(['git', 'add', pattern])


def git_commit(msg):
    subprocess.run(['git', 'commit', '-m', msg], capture_output=True)


def git_changed_files():
    ret = subprocess.check_output(['git', 'diff', '--name-only'], encoding='utf-8').split('\n')
    if ret[-1] == '':
        del ret[-1]
    return ret


maintainers = sys.argv[1]
message = sys.argv[2].strip()

subsystem = None

remap = {
    'Block layer core': 'block',
    'Block Jobs': 'block',
    'Dirty Bitmaps': 'block',
    'Block QAPI, monitor, command line': 'block',
    'Block I/O path': 'block',
    'Throttling infrastructure': 'block',
    'Architecture support': 's390x',
    'Guest CPU Cores (KVM)': 'kvm',
    'Guest CPU Cores (Xen)': 'xen',
    'Guest CPU cores (TCG)': 'tcg',
    'Network Block Device (NBD)': 'nbd',
    'Parallel NOR Flash devices': 'pflash',
    'Firmware configuration (fw_cfg)': 'fw_cfg',
    'Block SCSI subsystem': 'scsi',
    'Network device backends': 'net',
    'Netmap network backend': 'net',
    'Host Memory Backends': 'hostmem',
    'Cryptodev Backends': 'cryptodev',
    'QEMU Guest Agent': 'qga',
    'COLO Framework': 'colo',
    'Command line option argument parsing': 'cmdline',
    'Character device backends': 'chardev'
}


class Maintainers:
    def add(self, subsystem, path, mapper, mapper_name, glob_count=1):
        if subsystem in remap:
            subsystem = remap[subsystem]
        if subsystem not in self.subsystems:
            self.subsystems.append(subsystem)

        if path[-1] == '/':
            path = path[:-1]

        if path in mapper:
            if mapper[path][1] == glob_count:
                print('Warning: "{}" both in "{}" and "{}" in {} mapper with '
                      'same glob-count={}. {} ignored for this path.'.format(
                        path, mapper[path][0], subsystem, mapper_name, glob_count,
                          subsystem))
                return
            if mapper[path][1] < glob_count:
                # silently ignore worse match
                return

        mapper[path] = (subsystem, glob_count)

    def __init__(self, file_name):
        self.map_file = {}
        self.map_glob_file = {}
        self.map_dir = {}
        self.map_glob_dir = {}
        self.map_unmaintained_dir = {
            'python': ('python', 1),
            'hw/misc': ('misc', 1)
        }
        self.subsystems = ['python', 'misc']
        subsystem = None

        with open(file_name) as f:
            mode2 = False
            prevline = ''
            for line in f:
                line = line.rstrip()
                if not line:
                    continue
                if len(line) >= 2 and line[1] == ':':
                    if line[0] == 'F':
                        fname = line[3:]
                        if fname in ['*', '*/']:
                            continue
                        if os.path.isfile(fname):
                            self.add(subsystem, fname, self.map_file, 'file')
                        elif os.path.isdir(fname):
                            self.add(subsystem, fname, self.map_dir, 'dir')
                        else:
                            paths = glob.glob(fname)
                            if not paths:
                                print('Warning: nothing corresponds to "{}"'.format(fname))
                                continue

                            n = len(paths)
                            for f in paths:
                                if os.path.isfile(f):
                                    self.add(subsystem, f, self.map_glob_file, 'glob-file', n)
                                else:
                                    assert os.path.isdir(f)
                                    self.add(subsystem, f, self.map_glob_dir, 'glob-dir', n)
                elif line[:3] == '---':
                    subsystem = prevline
                    if subsystem == 'Devices':
                        mode2 = True
                elif mode2:
                    subsystem = line
                prevline = line

    def find_in_map_dir(self, file_name, mapper):
        while file_name != '' and file_name not in mapper:
            file_name = os.path.dirname(file_name)

        return None if file_name == '' else mapper[file_name][0]

    def find_in_map_file(self, file_name, mapper):
        if file_name in mapper:
            return mapper[file_name][0]

    def find_subsystem(self, file_name):
        s = self.find_in_map_file(file_name, self.map_file)
        if s is not None:
            return s

        s = self.find_in_map_file(file_name, self.map_glob_file)
        if s is not None:
            return s

        s = self.find_in_map_dir(file_name, self.map_dir)
        if s is not None:
            return s

        s = self.find_in_map_dir(file_name, self.map_glob_dir)
        if s is not None:
            return s

        s = self.find_in_map_dir(file_name, self.map_unmaintained_dir)
        if s is not None:
            return s

        self.subsystems.append(file_name)
        return file_name


def commit(subsystem):
    msg = subsystem
    if msg in remap:
        msg = remap[msg]
    msg += ': ' + message
    git_commit(msg)

mnt = Maintainers(maintainers)
res = {}
for f in git_changed_files():
    s = mnt.find_subsystem(f)
    if s in res:
        res[s].append(f)
    else:
        res[s] = [f]

for s in mnt.subsystems:
    if s in res:
        print(s)
        for f in res[s]:
            print('  ', f)

for s in mnt.subsystems:
    if s in res:
        for f in res[s]:
            git_add(f)
        commit(s)
