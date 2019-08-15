#
# Ensure CPU topology parameters may be omitted on -device
#
#  Copyright (c) 2019 Red Hat Inc
#
# Author:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, see <http://www.gnu.org/licenses/>.
#

from avocado_qemu import Test

class OmittedCPUProps(Test):
    """
    :avocado: tags=arch:x86_64
    """
    def test_only_socket(self):
        self.vm.add_args('-nodefaults', '-S')
        self.vm.add_args('-smp', '1,sockets=2,maxcpus=2')
        self.vm.add_args('-cpu', 'qemu64')
        self.vm.add_args('-device', 'qemu64-x86_64-cpu,socket-id=1')
        self.vm.launch()
        self.assertEquals(len(self.vm.command('query-cpus')), 2)

    def test_only_die(self):
        self.vm.add_args('-nodefaults', '-S')
        self.vm.add_args('-smp', '1,dies=2,maxcpus=2')
        self.vm.add_args('-cpu', 'qemu64')
        self.vm.add_args('-device', 'qemu64-x86_64-cpu,die-id=1')
        self.vm.launch()
        self.assertEquals(len(self.vm.command('query-cpus')), 2)

    def test_only_core(self):
        self.vm.add_args('-nodefaults', '-S')
        self.vm.add_args('-smp', '1,cores=2,maxcpus=2')
        self.vm.add_args('-cpu', 'qemu64')
        self.vm.add_args('-device', 'qemu64-x86_64-cpu,core-id=1')
        self.vm.launch()
        self.assertEquals(len(self.vm.command('query-cpus')), 2)

    def test_only_thread(self):
        self.vm.add_args('-nodefaults', '-S')
        self.vm.add_args('-smp', '1,threads=2,maxcpus=2')
        self.vm.add_args('-cpu', 'qemu64')
        self.vm.add_args('-device', 'qemu64-x86_64-cpu,thread-id=1')
        self.vm.launch()
        self.assertEquals(len(self.vm.command('query-cpus')), 2)
