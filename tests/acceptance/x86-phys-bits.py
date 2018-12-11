# Test for host-phys-bits-limit option
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.
import re

from avocado_qemu import Test

class HostPhysbits(Test):
    """
    Check if `host-phys-bits` and `host-phys-bits` options work.

    :avocado: enable
    :avocado: tags=x86_64
    """

    def cpu_qom_get(self, prop):
        qom_path = self.vm.command('query-cpus')[0]['qom_path']
        return self.vm.command('qom-get', path=qom_path, property=prop)

    def cpu_phys_bits(self):
        return self.cpu_qom_get('phys-bits')

    def host_phys_bits(self):
        cpuinfo = open('/proc/cpuinfo', 'rb').read()
        m = re.search(b'([0-9]+) bits physical', cpuinfo)
        if m is None:
            self.cancel("Couldn't read phys-bits from /proc/cpuinfo")
        return int(m.group(1))

    def setUp(self):
        super(HostPhysbits, self).setUp()
        self.vm.add_args('-S', '-machine', 'accel=kvm:tcg')
        self.vm.launch()
        if not self.vm.command('query-kvm')['enabled']:
            self.cancel("Test case requires KVM")
        self.vm.shutdown()


    def test_no_host_phys_bits(self):
        # default should be phys-bits=40 if host-phys-bits=off
        self.vm.add_args('-cpu', 'qemu64,host-phys-bits=off')
        self.vm.launch()
        self.assertEqual(self.cpu_phys_bits(), 40)

    def test_manual_phys_bits(self):
        self.vm.add_args('-cpu', 'qemu64,host-phys-bits=off,phys-bits=35')
        self.vm.launch()
        self.assertEqual(self.cpu_phys_bits(), 35)

    def test_host_phys_bits(self):
        host_phys_bits = self.host_phys_bits()
        self.vm.add_args('-cpu', 'qemu64,host-phys-bits=on')
        self.vm.launch()
        self.assertEqual(self.cpu_phys_bits(), host_phys_bits)

    def test_host_phys_bits_limit_high(self):
        hbits = self.host_phys_bits()
        self.vm.add_args('-cpu', 'qemu64,host-phys-bits=on,'
                                 'host-phys-bits-limit=%d' % (hbits + 1))
        self.vm.launch()
        self.assertEqual(self.cpu_phys_bits(), hbits)

    def test_host_phys_bits_limit_equal(self):
        hbits = self.host_phys_bits()
        self.vm.add_args('-cpu', 'qemu64,host-phys-bits=on,'
                                 'host-phys-bits-limit=%d' % (hbits))
        self.vm.launch()
        self.assertEqual(self.cpu_phys_bits(), hbits)

    def test_host_phys_bits_limit_low(self):
        hbits = self.host_phys_bits()
        self.vm.add_args('-cpu', 'qemu64,host-phys-bits=on,'
                                 'host-phys-bits-limit=%d' % (hbits - 1))
        self.vm.launch()
        self.assertEqual(self.cpu_phys_bits(), hbits - 1)
