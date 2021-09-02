# Test for the hmp command "info neighbors"
#
# Copyright 2021 Google LLC
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import re

from avocado_qemu import LinuxTest
from avocado_qemu import Test

VNET_HUB_HEADER = 'Hub -1 (vnet):'
NEIGHBOR_HEADER_REGEX = '^ *Table *MacAddr *IP Address$'

def trim(text):
    return " ".join(text.split())

def hmc(test, cmd):
    return test.vm.command('human-monitor-command', command_line=cmd)

def get_neighbors(test):
    output = hmc(test, 'info neighbors').splitlines()
    if len(output) < 2:
        test.fail("Insufficient output from 'info neighbors'")
    test.assertEquals(output[0], VNET_HUB_HEADER)
    test.assertTrue(re.fullmatch(NEIGHBOR_HEADER_REGEX, output[1]))
    return output[2:]

class InfoNeighborsNone(Test):

    def test_no_neighbors(self):
        self.vm.add_args('-nodefaults',
                         '-netdev', 'user,id=vnet',
                         '-device', 'virtio-net,netdev=vnet')
        self.vm.launch()
        neighbors = get_neighbors(self)
        self.assertEquals(len(neighbors), 0)

class InfoNeighbors(LinuxTest):

    def test_neighbors(self):
        """
        :avocado: tags=arch:x86_64
        :avocado: tags=machine:pc
        :avocado: tags=accel:kvm
        """
        self.require_accelerator('kvm')
        self.vm.add_args("-accel", "kvm")
        self.vm.add_args('-nographic',
                         '-m', '1024')
        self.launch_and_wait()

        # Ensure there's some packets to the guest and back.
        self.ssh_command('pwd')

        # We should now be aware of the guest as a neighbor.
        expected_ipv4_neighbor = 'ARP 52:54:00:12:34:56 10.0.2.15'
        # The default ipv6 net is fec0. Both fe80 and fec0 can appear.
        expected_ipv6_neighbors = [
            'NDP 52:54:00:12:34:56 fe80::5054:ff:fe12:3456',
            'NDP 52:54:00:12:34:56 fec0::5054:ff:fe12:3456'
        ]
        neighbors = get_neighbors(self)
        self.assertTrue(len(neighbors) >= 2 and len(neighbors) <= 3)
        # IPv4 is output first.
        self.assertEquals(trim(neighbors[0]), expected_ipv4_neighbor)
        for neighbor in neighbors[1:]:
            self.assertTrue(trim(neighbor) in expected_ipv6_neighbors)
