#!/usr/bin/env python3
#
# Map file virtual offset to the offset on the underlying block device.
# Works by parsing 'filefrag' output.
#
# Copyright (c) 2024 Virtuozzo International GmbH.
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

import argparse
import os
import subprocess
import re
import sys

from bisect import bisect_right
from collections import namedtuple
from dataclasses import dataclass
from shutil import which
from stat import S_ISBLK


Partition = namedtuple('Partition', ['partpath', 'diskpath', 'part_offt'])


@dataclass
class Extent:
    '''Class representing an individual file extent.

    This is basically a piece of data within the file which is located
    consecutively (i.e. not sparsely) on the underlying block device.
    '''

    log_start:  int
    log_end:    int
    phys_start: int
    phys_end:   int
    length:     int
    partition:  Partition

    @property
    def disk_start(self):
        'Number of the first byte of this extent on the whole disk (/dev/sda)'
        return self.partition.part_offt + self.phys_start

    @property
    def disk_end(self):
        'Number of the last byte of this extent on the whole disk (/dev/sda)'
        return self.partition.part_offt + self.phys_end

    def __str__(self):
        ischunk = self.log_end > self.log_start
        maybe_end = lambda s: f'..{s}' if ischunk else ''
        return '%s%s (file)  ->  %s%s (%s)  ->  %s%s (%s)' % (
            self.log_start, maybe_end(self.log_end),
            self.phys_start, maybe_end(self.phys_end), self.partition.partpath,
            self.disk_start, maybe_end(self.disk_end), self.partition.diskpath
        )

    @classmethod
    def ext_slice(cls, bigger_ext, start, end):
        '''Constructor for the Extent class from a bigger extent.

        Return Extent instance which is a slice of @bigger_ext contained
        within the range [start, end].
        '''

        assert start >= bigger_ext.log_start
        assert end <= bigger_ext.log_end

        if start == bigger_ext.log_start and end == bigger_ext.log_end:
            return bigger_ext

        phys_start = bigger_ext.phys_start + (start - bigger_ext.log_start)
        phys_end = bigger_ext.phys_end - (bigger_ext.log_end - end)
        length = end - start + 1

        return cls(start, end, phys_start, phys_end, length,
                   bigger_ext.partition)


def run_cmd(cmd: str) -> str:
    '''Wrapper around subprocess.run.

    Returns stdout in case of success, emits en error and exits in case
    of failure.
    '''

    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          check=False, shell=True)
    if proc.stderr is not None:
        stderr = f'\n{proc.stderr.decode().strip()}'
    else:
        stderr = ''

    if proc.returncode:
        sys.exit(f'Error: Command "{cmd}" returned {proc.returncode}:{stderr}')

    return proc.stdout.decode().strip()


def parse_size(offset: str) -> int:
    'Convert human readable size to bytes'

    suffixes = {
        **dict.fromkeys(['k', 'K', 'Kb', 'KB', 'KiB'], 2 ** 10),
        **dict.fromkeys(['m', 'M', 'Mb', 'MB', 'MiB'], 2 ** 20),
        **dict.fromkeys(['g', 'G', 'Gb', 'GB', 'GiB'], 2 ** 30),
        **dict.fromkeys(     ['T', 'Tb', 'TB', 'TiB'], 2 ** 40),
        **dict.fromkeys([''],                          1)
    }

    sizematch = re.match(r'^([0-9]+)\s*([a-zA-Z]*)$', offset)
    if not bool(sizematch):
        sys.exit(f'Error: Couldn\'t parse size "{offset}". Pass offset '
                  'either in bytes or in format 1K, 2M, 3G')

    num, suff = sizematch.groups()
    num = int(num)

    mult = suffixes.get(suff)
    if mult is None:
        sys.exit(f'Error: Couldn\'t parse size "{offset}": '
                 f'unknown suffix {suff}')

    return num * mult


def fpath2part(filename: str) -> str:
    'Get partition on which @filename is located (i.e. /dev/sda1).'

    partpath = run_cmd(f'df --output=source {filename} | tail -n+2')
    if not os.path.exists(partpath) or not S_ISBLK(os.stat(partpath).st_mode):
        sys.exit(f'Error: file {filename} is located on {partpath} which '
                 'isn\'t a block device')
    return partpath


def part2dev(partpath: str, filename: str) -> str:
    'Get block device on which @partpath is located (i.e. /dev/sda).'
    dev = run_cmd(f'lsblk -no PKNAME {partpath}')
    diskpath = f'/dev/{dev}'
    if not os.path.exists(diskpath) or not S_ISBLK(os.stat(diskpath).st_mode):
        sys.exit(f'Error: file {filename} is located on {diskpath} which '
                 'isn\'t a block device')
    return diskpath


def part2disktype(partpath: str) -> str:
    'Parse /proc/devices and get block device type for @partpath'

    major = os.major(os.stat(partpath).st_rdev)
    assert major
    with open('/proc/devices', encoding='utf-8') as devf:
        for line in reversed(list(devf)):
            # Our major cannot be absent among block devs
            if line.startswith('Block'):
                break
            devmajor, devtype = line.strip().split()
            if int(devmajor) == major:
                return devtype

    sys.exit('Error: We haven\'t found major {major} in /proc/devices, '
             'and that can\'t be')


def get_part_offset(part: str, disk: str) -> int:
    'Get offset in bytes of the partition @part on the block device @disk.'

    lines = run_cmd(f'fdisk -l {disk} | egrep "^(Units|{part})"').splitlines()

    unitmatch = re.match('^.* = ([0-9]+) bytes$', lines[0])
    if not bool(unitmatch):
        sys.exit(f'Error: Couldn\'t parse "fdisk -l" output:\n{lines[0]}')
    secsize = int(unitmatch.group(1))

    part_offt = int(lines[1].split()[1])
    return part_offt * secsize


def parse_frag_line(line: str, partition: Partition) -> Extent:
    'Construct Extent instance from a "filefrag" output line.'

    nums = [int(n) for n in re.findall(r'[0-9]+', line)]

    log_start  = nums[1]
    log_end    = nums[2]
    phys_start = nums[3]
    phys_end   = nums[4]
    length     = nums[5]

    assert log_start < log_end
    assert phys_start < phys_end
    assert (log_end - log_start + 1) == (phys_end - phys_start + 1) == length

    return Extent(log_start, log_end, phys_start, phys_end, length, partition)


def preliminary_checks(args: argparse.Namespace) -> None:
    'A bunch of checks to emit an error and exit at the earlier stage.'

    if which('filefrag') is None:
        sys.exit('Error: Program "filefrag" doesn\'t exist')

    if not os.path.exists(args.filename):
        sys.exit(f'Error: File {args.filename} doesn\'t exist')

    args.filesize = os.path.getsize(args.filename)
    if args.offset >= args.filesize:
        sys.exit(f'Error: Specified offset {args.offset} exceeds '
                 f'file size {args.filesize}')
    if args.size and (args.offset + args.size > args.filesize):
        sys.exit(f'Error: Chunk of size {args.size} at offset '
                 f'{args.offset} exceeds file size {args.filesize}')

    args.partpath = fpath2part(args.filename)
    args.disktype = part2disktype(args.partpath)
    if args.disktype not in ('sd', 'virtblk'):
        sys.exit(f'Error: Cannot analyze files on {args.disktype} disks')
    args.diskpath = part2dev(args.partpath, args.filename)
    args.part_offt = get_part_offset(args.partpath, args.diskpath)


def get_extent_maps(args: argparse.Namespace) -> list[Extent]:
    'Run "filefrag", parse its output and return a list of Extent instances.'

    lines = run_cmd(f'filefrag -b1 -v {args.filename}').splitlines()

    ffinfo_re = re.compile('.* is ([0-9]+) .*of ([0-9]+) bytes')
    ff_size, ff_block = re.match(ffinfo_re, lines[1]).groups()

    # Paranoia checks
    if int(ff_size) != args.filesize:
        sys.exit('Error: filefrag and os.path.getsize() report different '
                 f'sizes: {ff_size} and {args.filesize}')
    if int(ff_block) != 1:
        sys.exit(f'Error: "filefrag -b1" invoked, but block size is {ff_block}')

    partition = Partition(args.partpath, args.diskpath, args.part_offt)

    # Fill extents list from the output
    extents = []
    for line in lines:
        if not re.match(r'^\s*[0-9]+:', line):
            continue
        extents += [parse_frag_line(line, partition)]

    chunk_start = args.offset
    chunk_end = args.offset + args.size - 1
    ext_offsets = [ext.log_start for ext in extents]
    start_ind = bisect_right(ext_offsets, chunk_start) - 1
    end_ind = bisect_right(ext_offsets, chunk_end) - 1

    res_extents = extents[start_ind : end_ind + 1]
    for i, ext in enumerate(res_extents):
        start = max(chunk_start, ext.log_start)
        end = min(chunk_end, ext.log_end)
        res_extents[i] = Extent.ext_slice(ext, start, end)

    return res_extents


def parse_args() -> argparse.Namespace:
    'Define program arguments and parse user input.'

    parser = argparse.ArgumentParser(description='''
Map file offset to physical offset on the block device

With --size provided get a list of mappings for the chunk''',
    formatter_class=argparse.RawTextHelpFormatter)

    parser.add_argument('filename', type=str, help='filename to process')
    parser.add_argument('offset', type=str,
                        help='logical offset inside the file')
    parser.add_argument('-s', '--size', required=False, type=str,
                        help='size of the file chunk to get offsets for')
    args = parser.parse_args()

    args.offset = parse_size(args.offset)
    if args.size:
        args.size = parse_size(args.size)
    else:
        # When no chunk size is provided (only offset), it's equivalent to
        # chunk size == 1
        args.size = 1

    return args


def main() -> int:
    args = parse_args()
    preliminary_checks(args)
    extents = get_extent_maps(args)
    for ext in extents:
        print(ext)


if __name__ == '__main__':
    sys.exit(main())
