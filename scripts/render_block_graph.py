#!/usr/bin/env python3
#
# Render Qemu Block Graph
#
# Copyright (c) 2018 Virtuozzo International GmbH. All rights reserved.
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

import os
import sys
import subprocess
import json
from graphviz import Digraph
import argparse

sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'python'))
from qemu.qmp import QMPError
from qemu.qmp.legacy import QEMUMonitorProtocol


def perm(arr):
    s = 'w' if 'write' in arr else '_'
    s += 'r' if 'consistent-read' in arr else '_'
    s += 'u' if 'write-unchanged' in arr else '_'
    s += 's' if 'resize' in arr else '_'
    return s


def render_block_graph(qmp, filename, format='png'):
    '''
    Render graph in text (dot) representation into "@filename" and
    representation in @format into "@filename.@format"
    '''

    bds_nodes = qmp.cmd('query-named-block-nodes')
    bds_nodes = {n['node-name']: n for n in bds_nodes}

    job_nodes = qmp.cmd('query-block-jobs')
    job_nodes = {n['device']: n for n in job_nodes}

    block_graph = qmp.cmd('x-debug-query-block-graph')

    graph = Digraph(comment='Block Nodes Graph')
    graph.format = format
    graph.node(r'permission symbols:\l'
               r'  w - Write\l'
               r'  r - consistent-Read\l'
               r'  u - write - Unchanged\l'
               r'  g - Graph-mod\l'
               r'  s - reSize\l'
               r'edge label scheme:\l'
               r'  <child type>\l'
               r'  <perm>\l'
               r'  <shared_perm>\l', shape='none')

    for n in block_graph['nodes']:
        if n['type'] == 'block-driver':
            info = bds_nodes[n['name']]
            label = n['name'] + ' [' + info['drv'] + ']'
            if info['drv'] == 'file':
                label += '\n' + os.path.basename(info['file'])
            shape = 'ellipse'
        elif n['type'] == 'block-job':
            info = job_nodes[n['name']]
            label = info['type'] + ' job (' + n['name'] + ')'
            shape = 'box'
        else:
            assert n['type'] == 'block-backend'
            label = n['name'] if n['name'] else 'unnamed blk'
            shape = 'box'

        graph.node(str(n['id']), label, shape=shape)

    for e in block_graph['edges']:
        label = r'%s\l%s\l%s\l' % (e['name'], perm(e['perm']),
                                   perm(e['shared-perm']))
        graph.edge(str(e['parent']), str(e['child']), label=label)

    graph.render(filename)


class LibvirtGuest():
    def __init__(self, name, uri=None):
        self.name = name
        self.uri = uri

    def cmd(self, cmd):
        # only supports qmp commands without parameters
        m = {'execute': cmd}
        ar = ['virsh']

        if self.uri:
            ar += ['-c', self.uri]

        ar += ['qemu-monitor-command', self.name, json.dumps(m)]

        reply = json.loads(subprocess.check_output(ar))

        if 'error' in reply:
            raise QMPError(reply)

        return reply['return']


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
            description='Tool that renders the qemu block graph into a image.')

    modegroup = parser.add_mutually_exclusive_group(required=True)

    modegroup.add_argument('--socket',
                           help='socket mode - path to qemu monitor socket')
    modegroup.add_argument('--vm', help='libvirt mode - name of libvirt VM')

    parser.add_argument('--uri', help='libvirt URI to connect to')

    parser.add_argument('--output',
                        help='path to output image (.png suffix added); '
                             'in libvirt mode default is the name of the VM')

    args = parser.parse_args()

    if args.socket:
        if not args.output:
            print("--output required in socket mode", file=sys.stderr)
            parser.print_usage()
            sys.exit(1)

        qmp = QEMUMonitorProtocol(args.socket)
        qmp.connect()
        out = args.output

    if args.vm:
        qmp = LibvirtGuest(args.vm, args.uri)

        if args.output:
            out = args.output
        else:
            out = args.vm

    render_block_graph(qmp, out)
