#!/usr/bin/env python
#
# Basic validation of x86 versioned CPU models and CPU model aliases
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


import avocado_qemu

def get_cpu_prop(vm, prop):
    cpu_path = vm.command('query-cpus')[0].get('qom_path')
    return vm.command('qom-get', path=cpu_path, property=prop)

class X86CPUModelAliases(avocado_qemu.Test):
    """
    Validation of PC CPU model versions and CPU model aliases

    :avocado: tags=arch:x86_64
    """
    def test_4_0_alias(self):
        """Check if pc-*-4.0 unversioned CPU model won't be an alias"""
        # pc-*-4.0 won't expose non-versioned CPU models as aliases
        # We do this to help management software to keep compatibility
        # with older QEMU versions that didn't have the versioned CPU model
        self.vm.add_args('-S')
        self.vm.set_machine('pc-i440fx-4.0')
        self.vm.launch()

        cpus = dict((m['name'], m) for m in self.vm.command('query-cpu-definitions'))

        self.assertFalse(cpus['Cascadelake-Server']['static'],
                         'unversioned Cascadelake-Server CPU model must not be static')
        self.assertNotIn('alias-of', cpus['Cascadelake-Server'],
                         'Cascadelake-Server must not be an alias')
        self.assertNotIn('alias-of', cpus['Cascadelake-Server-4.1'],
                         'Cascadelake-Server-4.1 must not be an alias')

        self.assertFalse(cpus['qemu64']['static'],
                         'unversioned qemu64 CPU model must not be static')
        self.assertNotIn('alias-of', cpus['qemu64'],
                         'qemu64 must not be an alias')
        self.assertNotIn('alias-of', cpus['qemu64-4.1'],
                         'qemu64-4.1 must not be an alias')

    def test_4_1_alias(self):
        """Check if unversioned CPU model is an alias pointing to 4.1 version"""
        self.vm.add_args('-S')
        self.vm.set_machine('pc-i440fx-4.1')
        self.vm.launch()

        cpus = dict((m['name'], m) for m in self.vm.command('query-cpu-definitions'))

        self.assertFalse(cpus['Cascadelake-Server']['static'],
                         'unversioned Cascadelake-Server CPU model must not be static')
        self.assertEquals(cpus['Cascadelake-Server'].get('alias-of'), 'Cascadelake-Server-4.1',
                          'Cascadelake-Server must be an alias of Cascadelake-Server-4.1')
        self.assertNotIn('alias-of', cpus['Cascadelake-Server-4.1'],
                         'Cascadelake-Server-4.1 must not be an alias')

        self.assertFalse(cpus['qemu64']['static'],
                         'unversioned qemu64 CPU model must not be static')
        self.assertEquals(cpus['qemu64'].get('alias-of'), 'qemu64-4.1',
                          'qemu64 must be an alias of qemu64-4.1')
        self.assertNotIn('alias-of', cpus['qemu64-4.1'],
                         'qemu64-4.1 must not be an alias')

    def test_none_alias(self):
        """Check if unversioned CPU model is an alias pointing to 4.1 version"""
        self.vm.add_args('-S')
        self.vm.set_machine('none')
        self.vm.launch()

        cpus = dict((m['name'], m) for m in self.vm.command('query-cpu-definitions'))

        self.assertFalse(cpus['Cascadelake-Server']['static'],
                         'unversioned Cascadelake-Server CPU model must not be static')
        self.assertTrue(cpus['Cascadelake-Server']['alias-of'].startswith('Cascadelake-Server-'),
                          'Cascadelake-Server must be an alias of versioned CPU model')
        self.assertNotIn('alias-of', cpus['Cascadelake-Server-4.1'],
                         'Cascadelake-Server-4.1 must not be an alias')

        self.assertFalse(cpus['qemu64']['static'],
                         'unversioned qemu64 CPU model must not be static')
        self.assertTrue(cpus['qemu64']['alias-of'].startswith('qemu64-'),
                          'qemu64 must be an alias of versioned CPU model')
        self.assertNotIn('alias-of', cpus['qemu64-4.1'],
                         'qemu64-4.1 must not be an alias')

    def test_Cascadelake_arch_capabilities_result(self):
        # machine-type only:
        vm = self.get_vm()
        vm.add_args('-S')
        vm.set_machine('pc-i440fx-4.1')
        vm.add_args('-cpu', 'Cascadelake-Server,x-force-features=on,check=off,enforce=off')
        vm.launch()
        self.assertFalse(get_cpu_prop(vm, 'arch-capabilities'),
                         'pc-i440fx-4.1 + Cascadelake-Server should not have arch-capabilities')

        vm = self.get_vm()
        vm.add_args('-S')
        vm.set_machine('pc-i440fx-4.0')
        vm.add_args('-cpu', 'Cascadelake-Server,x-force-features=on,check=off,enforce=off')
        vm.launch()
        self.assertFalse(get_cpu_prop(vm, 'arch-capabilities'),
                         'pc-i440fx-4.0 + Cascadelake-Server should not have arch-capabilities')

        # command line must override machine-type if CPU model is not versioned:
        vm = self.get_vm()
        vm.add_args('-S')
        vm.set_machine('pc-i440fx-4.0')
        vm.add_args('-cpu', 'Cascadelake-Server,x-force-features=on,check=off,enforce=off,+arch-capabilities')
        vm.launch()
        self.assertTrue(get_cpu_prop(vm, 'arch-capabilities'),
                        'pc-i440fx-4.0 + Cascadelake-Server,+arch-capabilities should have arch-capabilities')

        vm = self.get_vm()
        vm.add_args('-S')
        vm.set_machine('pc-i440fx-4.1')
        vm.add_args('-cpu', 'Cascadelake-Server,x-force-features=on,check=off,enforce=off,-arch-capabilities')
        vm.launch()
        self.assertFalse(get_cpu_prop(vm, 'arch-capabilities'),
                         'pc-i440fx-4.1 + Cascadelake-Server,-arch-capabilities should not have arch-capabilities')

        # versioned CPU model overrides machine-type:
        vm = self.get_vm()
        vm.add_args('-S')
        vm.set_machine('pc-i440fx-4.0')
        vm.add_args('-cpu', 'Cascadelake-Server-4.1,x-force-features=on,check=off,enforce=off')
        vm.launch()
        self.assertFalse(get_cpu_prop(vm, 'arch-capabilities'),
                         'pc-i440fx-4.1 + Cascadelake-Server-4.1 should not have arch-capabilities')

        vm = self.get_vm()
        vm.add_args('-S')
        vm.set_machine('pc-i440fx-4.0')
        vm.add_args('-cpu', 'Cascadelake-Server-4.1.1,x-force-features=on,check=off,enforce=off')
        vm.launch()
        self.assertTrue(get_cpu_prop(vm, 'arch-capabilities'),
                         'pc-i440fx-4.1 + Cascadelake-Server-4.1 should have arch-capabilities')

        # command line must override machine-type and versioned CPU model:
        vm = self.get_vm()
        vm.add_args('-S')
        vm.set_machine('pc-i440fx-4.0')
        vm.add_args('-cpu', 'Cascadelake-Server,x-force-features=on,check=off,enforce=off,+arch-capabilities')
        vm.launch()
        self.assertTrue(get_cpu_prop(vm, 'arch-capabilities'),
                         'pc-i440fx-4.0 + Cascadelake-Server-4.1,+arch-capabilities should have arch-capabilities')

        vm = self.get_vm()
        vm.add_args('-S')
        vm.set_machine('pc-i440fx-4.1')
        vm.add_args('-cpu', 'Cascadelake-Server-4.1.1,x-force-features=on,check=off,enforce=off,-arch-capabilities')
        vm.launch()
        self.assertFalse(get_cpu_prop(vm, 'arch-capabilities'),
                         'pc-i440fx-4.1 + Cascadelake-Server-4.1.1,-arch-capabilities should not have arch-capabilities')
