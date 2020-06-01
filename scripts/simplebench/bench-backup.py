#!/usr/bin/env python3
#
# Bench backup block-job
#
# Copyright (c) 2020 Virtuozzo International GmbH.
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
import simplebench
from bench_block_job import bench_block_copy, drv_file, drv_nbd


def bench_func(env, case):
    """ Handle one "cell" of benchmarking table. """
    cmd_options = env['cmd-options'] if 'cmd-options' in env else {}
    return bench_block_copy(env['qemu-binary'], env['cmd'],
                            cmd_options,
                            case['source'], case['target'])


def bench(args):
    test_cases = []

    sources = {}
    targets = {}
    for d in args.dir:
        label, path = d.split(':')
        sources[label] = drv_file(path + '/test-source')
        targets[label] = drv_file(path + '/test-target')

    if args.nbd:
        nbd = args.nbd.split(':')
        host = nbd[0]
        port = '10809' if len(nbd) == 1 else nbd[1]
        drv = drv_nbd(host, port)
        sources['nbd'] = drv
        targets['nbd'] = drv

    for t in args.test:
        src, dst = t.split(':')

        test_cases.append({
            'id': t,
            'source': sources[src],
            'target': targets[dst]
        })

    binaries = []
    upstream = None
    for i, q in enumerate(args.qemu):
        name_path = q.split(':')
        if len(name_path) == 1:
            binaries.append((f'q{i}', name_path[0]))
        else:
            binaries.append((name_path[0], name_path[1]))
            if name_path[0] == 'upstream' or name_path[0] == 'master':
                upstream = binaries[-1]

    test_envs = []
    if upstream:
        label, path = upstream
        test_envs.append({
                'id': f'mirror({label})',
                'cmd': 'blockdev-mirror',
                'qemu-binary': path
            })

    for label, path in binaries:
        test_envs.append({
            'id': f'backup({label})',
            'cmd': 'blockdev-backup',
            'qemu-binary': path
        })
        test_envs.append({
            'id': f'backup({label}, no-copy-range)',
            'cmd': 'blockdev-backup',
            'cmd-options': {'x-use-copy-range': False},
            'qemu-binary': path
        })
        if label == 'new':
            test_envs.append({
                'id': f'backup({label}, copy-range-1w)',
                'cmd': 'blockdev-backup',
                'cmd-options': {'x-use-copy-range': True,
                                'x-max-workers': 1},
                'qemu-binary': path
            })

    result = simplebench.bench(bench_func, test_envs, test_cases, count=3)
    print(simplebench.ascii(result))


class ExtendAction(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
        items = getattr(namespace, self.dest) or []
        items.extend(values)
        setattr(namespace, self.dest, items)


if __name__ == '__main__':
    p = argparse.ArgumentParser('Backup benchmark')
    p.add_argument('--qemu', nargs='+', help='Qemu binaries to compare, just '
                   'file path with label, like label:/path/to/qemu. Qemu '
                   'labeled "new" should support x-max-workers argument for '
                   'backup job, labeled "upstream" will be used also to run '
                   'mirror benchmark for comparison.',
                   action=ExtendAction)
    p.add_argument('--dir', nargs='+', help='Directories, each containing '
                   '"test-source" and/or "test-target" files, raw images to '
                   'used in benchmarking. File path with label, like '
                   'label:/path/to/directory', action=ExtendAction)
    p.add_argument('--nbd', help='host:port for remote NBD '
                   'image, (or just host, for default port 10809). Use it in '
                   'tests, label is "nbd" (but you cannot create test '
                   'nbd:nbd).')
    p.add_argument('--test', nargs='+', help='Tests, in form '
                   'source-dir-label:target-dir-label', action=ExtendAction)

    bench(p.parse_args())
