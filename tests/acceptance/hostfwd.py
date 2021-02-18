# Hostfwd command tests
#
# Copyright 2021 Google LLC
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
# for more details.


from avocado_qemu import Test


class Hostfwd(Test):
    """
    :avocado: tags=hostfwd
    """
    def hmc(self, cmd):
        return self.vm.command('human-monitor-command', command_line=cmd)

    def test_qmp_hostfwd_ipv4(self):
        self.vm.add_args('-nodefaults',
                         '-netdev', 'user,id=vnet',
                         '-device', 'virtio-net,netdev=vnet')
        self.vm.launch()
        self.assertEquals(self.hmc('hostfwd_add vnet tcp::65022-:22'), '')
        self.assertEquals(self.hmc('hostfwd_remove vnet tcp::65022'),
                          'host forwarding rule for tcp::65022 removed\r\n')
        self.assertEquals(self.hmc('hostfwd_add tcp::65022-:22'), '')
        self.assertEquals(self.hmc('hostfwd_remove tcp::65022'),
                          'host forwarding rule for tcp::65022 removed\r\n')
        self.assertEquals(self.hmc('hostfwd_add udp::65042-:42'), '')
        self.assertEquals(self.hmc('hostfwd_remove udp::65042'),
                          'host forwarding rule for udp::65042 removed\r\n')

    def test_qmp_hostfwd_ipv4_functional_errors(self):
        """Verify handling of various kinds of errors given valid addresses."""
        self.vm.add_args('-nodefaults',
                         '-netdev', 'user,id=vnet',
                         '-device', 'virtio-net,netdev=vnet')
        self.vm.launch()
        self.assertEquals(self.hmc('hostfwd_remove ::65022'),
                          'host forwarding rule for ::65022 not found\r\n')
        self.assertEquals(self.hmc('hostfwd_add udp::65042-:42'), '')
        self.assertEquals(self.hmc('hostfwd_add udp::65042-:42'),
                          "Could not set up host forwarding rule 'udp::65042-:42'\r\n")
        self.assertEquals(self.hmc('hostfwd_remove ::65042'),
                          'host forwarding rule for ::65042 not found\r\n')
        self.assertEquals(self.hmc('hostfwd_remove udp::65042'),
                          'host forwarding rule for udp::65042 removed\r\n')
        self.assertEquals(self.hmc('hostfwd_remove udp::65042'),
                          'host forwarding rule for udp::65042 not found\r\n')

    def test_qmp_hostfwd_ipv4_parsing_errors(self):
        """Verify handling of various kinds of parsing errors."""
        self.vm.add_args('-nodefaults',
                         '-netdev', 'user,id=vnet',
                         '-device', 'virtio-net,netdev=vnet')
        self.vm.launch()
        self.assertEquals(self.hmc('hostfwd_remove abc::42'),
                          'Invalid format: Bad protocol name\r\n')
        self.assertEquals(self.hmc('hostfwd_add abc::65022-:22'),
                          "Invalid host forwarding rule 'abc::65022-:22' (Bad protocol name)\r\n")
        self.assertEquals(self.hmc('hostfwd_add :a.b.c.d:66-:66'),
                          "Invalid host forwarding rule ':a.b.c.d:66-:66' (For host address: Bad address)\r\n")
        self.assertEquals(self.hmc('hostfwd_add ::66-a.b.c.d:66'),
                          "Invalid host forwarding rule '::66-a.b.c.d:66' (For guest address: Bad address)\r\n")
        self.assertEquals(self.hmc('hostfwd_add ::66666-:66666'),
                          "Invalid host forwarding rule '::66666-:66666' (For host address: Bad port)\r\n")
        self.assertEquals(self.hmc('hostfwd_add ::-1-foo'),
                          "Invalid host forwarding rule '::-1-foo' (For host address: error parsing host:port ':')\r\n")
        self.assertEquals(self.hmc('hostfwd_add ::66-foo'),
                          "Invalid host forwarding rule '::66-foo' (For guest address: error parsing host:port 'foo')\r\n")
        self.assertEquals(self.hmc('hostfwd_add ::66-:66666'),
                          "Invalid host forwarding rule '::66-:66666' (For guest address: Bad port)\r\n")
        self.assertEquals(self.hmc('hostfwd_add ::66-:-1'),
                          "Invalid host forwarding rule '::66-:-1' (For guest address: Bad port)\r\n")
        self.assertEquals(self.hmc('hostfwd_add ::66-:0'),
                          "Invalid host forwarding rule '::66-:0' (For guest address: Bad port)\r\n")

    def test_qmp_hostfwd_ipv6(self):
        self.vm.add_args('-nodefaults',
                         '-netdev', 'user,id=vnet',
                         '-device', 'virtio-net,netdev=vnet')
        self.vm.launch()
        self.assertEquals(self.hmc('hostfwd_add vnet tcp:[::1]:65022-[fe80::1]:22'),
                          '')
        self.assertEquals(self.hmc('hostfwd_remove vnet tcp:[::1]:65022'),
                          'host forwarding rule for tcp:[::1]:65022 removed\r\n')
        self.assertEquals(self.hmc('hostfwd_add tcp:[]:65042-[fe80::1]:42'),
                          '')
        self.assertEquals(self.hmc('hostfwd_remove tcp:[]:65042'),
                          'host forwarding rule for tcp:[]:65042 removed\r\n')
        self.assertEquals(self.hmc('hostfwd_add udp:[::1]:65042-[fe80::1]:42'),
                          '')
        self.assertEquals(self.hmc('hostfwd_remove udp:[::1]:65042'),
                          'host forwarding rule for udp:[::1]:65042 removed\r\n')

    def test_qmp_hostfwd_ipv6_functional_errors(self):
        """Verify handling of various kinds of errors given valid addresses."""
        self.vm.add_args('-nodefaults',
                         '-netdev', 'user,id=vnet',
                         '-device', 'virtio-net,netdev=vnet')
        self.vm.launch()
        self.assertEquals(self.hmc('hostfwd_remove :[::1]:65022'),
                          'host forwarding rule for :[::1]:65022 not found\r\n')
        self.assertEquals(self.hmc('hostfwd_add udp:[::1]:65042-[fe80::1]:42'),
                          '')
        self.assertEquals(self.hmc('hostfwd_add udp:[::1]:65042-[fe80::1]:42'),
                          "Could not set up host forwarding rule 'udp:[::1]:65042-[fe80::1]:42'\r\n")
        self.assertEquals(self.hmc('hostfwd_remove :[::1]:65042'),
                          'host forwarding rule for :[::1]:65042 not found\r\n')
        self.assertEquals(self.hmc('hostfwd_remove udp:[::1]:65042'),
                          'host forwarding rule for udp:[::1]:65042 removed\r\n')
        self.assertEquals(self.hmc('hostfwd_remove udp:[::1]:65042'),
                          'host forwarding rule for udp:[::1]:65042 not found\r\n')

    def test_qmp_hostfwd_ipv6_errors(self):
        """Verify handling of various kinds of errors."""
        self.vm.add_args('-nodefaults',
                         '-netdev', 'user,id=vnet',
                         '-device', 'virtio-net,netdev=vnet')
        self.vm.launch()
        self.assertEquals(self.hmc('hostfwd_add :[::1-'),
                          "Invalid host forwarding rule ':[::1-' (For host address: error parsing IPv6 host:port '[::1')\r\n")
        self.assertEquals(self.hmc('hostfwd_add :[::1]:66-[fe80::1'),
                          "Invalid host forwarding rule ':[::1]:66-[fe80::1' (For guest address: error parsing IPv6 host:port '[fe80::1')\r\n")
        self.assertEquals(self.hmc('hostfwd_add :[:::]:66-foo'),
                          "Invalid host forwarding rule ':[:::]:66-foo' (For host address: Bad address)\r\n")
        self.assertEquals(self.hmc('hostfwd_add :[::1]-foo'),
                          "Invalid host forwarding rule ':[::1]-foo' (For host address: error parsing IPv6 host:port '[::1]')\r\n")
        self.assertEquals(self.hmc('hostfwd_add :[::1]:66-[foo]'),
                          "Invalid host forwarding rule ':[::1]:66-[foo]' (For guest address: error parsing IPv6 host:port '[foo]')\r\n")
        self.assertEquals(self.hmc('hostfwd_add :[::1]:66666-foo'),
                          "Invalid host forwarding rule ':[::1]:66666-foo' (For host address: Bad port)\r\n")
        self.assertEquals(self.hmc('hostfwd_add :[::1]:66-[fe80::1]:-1'),
                          "Invalid host forwarding rule ':[::1]:66-[fe80::1]:-1' (For guest address: Bad port)\r\n")
        self.assertEquals(self.hmc('hostfwd_add :[::1]:66-[fe80::1]:66666'),
                          "Invalid host forwarding rule ':[::1]:66-[fe80::1]:66666' (For guest address: Bad port)\r\n")
        self.assertEquals(self.hmc('hostfwd_add :[::1]:66-[fe80::1]:0'),
                          "Invalid host forwarding rule ':[::1]:66-[fe80::1]:0' (For guest address: Bad port)\r\n")
        self.assertEquals(self.hmc('hostfwd_add :[::1]:66-1.2.3.4:66'),
                          "Invalid host forwarding rule ':[::1]:66-1.2.3.4:66' (Both host,guest must be one of ipv4 or ipv6)\r\n")
        self.assertEquals(self.hmc('hostfwd_add :1.2.3.4:66-[fe80::1]:66'),
                          "Invalid host forwarding rule ':1.2.3.4:66-[fe80::1]:66' (Both host,guest must be one of ipv4 or ipv6)\r\n")
