# Regression test for host-nodes limit validation
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from avocado_qemu import Test
from subprocess import Popen, PIPE

MAX_NODES = 128

class HostNodesValidation(Test):
    def test_large_host_nodes(self):
        p = Popen([self.qemu_bin, '-display', 'none', '-nodefaults',
                   '-object', 'memory-backend-ram,id=m0,'
                              'size=4096,host-nodes=%d' % (MAX_NODES)],
                  stderr=PIPE, stdout=PIPE)
        stdout,stderr = p.communicate()

        self.assertIn(b'Invalid host-nodes', stderr)
        self.assertEquals(stdout, b'')
        self.assertEquals(p.returncode, 1)

    def test_valid_host_nodes(self):
        p = Popen([self.qemu_bin, '-display', 'none', '-nodefaults',
                   '-object', 'memory-backend-ram,id=m0,'
                              'size=4096,host-nodes=%d' % (MAX_NODES - 1)],
                  stderr=PIPE, stdout=PIPE)
        stdout,stderr = p.communicate()

        self.assertIn(b'host-nodes must be empty', stderr)
        self.assertEquals(p.returncode, 1)
