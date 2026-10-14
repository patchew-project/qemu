#!/usr/bin/env python3
#
# Validation of the hv-extcalls / hv-memory-hint CPU properties.
#
# hv-extcalls gates the Hyper-V extended hypercalls (HV_ENABLE_EXTENDED_
# HYPERCALLS, HV_CPUID_FEATURES.EBX bit 20); hv-memory-hint additionally
# gates whether HvExtCallQueryCapabilities advertises, and
# HvExtCallMemoryHeatHint accepts, the memory heat-hint calls.
#
# SPDX-License-Identifier: GPL-2.0-or-later
#

from qemu_test import QemuSystemTest


class HypervExtCallsProps(QemuSystemTest):
    """
    Validation of the hv-extcalls and hv-memory-hint CPU properties.
    """

    def get_cpu_prop(self, prop):
        cpu_path = self.vm.cmd('query-cpus-fast')[0].get('qom-path')
        return self.vm.cmd('qom-get', path=cpu_path, property=prop)

    def launch_with_cpu_props(self, cpu_props):
        self.set_machine('pc')
        self.vm.add_args('-S')
        self.set_vm_arg('-cpu', 'qemu64' + cpu_props)
        self.vm.launch()

    def test_defaults_off(self):
        """
        Neither property is requested: both must default to off, in
        particular hv-extcalls (which was deliberately made default-off
        so that enabling the extended hypercalls is an explicit opt-in).
        """
        self.launch_with_cpu_props('')
        self.assertFalse(self.get_cpu_prop('hv-extcalls'),
                         'hv-extcalls must default to off')
        self.assertFalse(self.get_cpu_prop('hv-memory-hint'),
                         'hv-memory-hint must default to off')

    def test_hv_extcalls_on(self):
        """
        hv-extcalls=on by itself must not implicitly turn on
        hv-memory-hint.
        """
        self.launch_with_cpu_props(',hv-extcalls=on')
        self.assertTrue(self.get_cpu_prop('hv-extcalls'),
                        'hv-extcalls=on must be reflected on the CPU')
        self.assertFalse(self.get_cpu_prop('hv-memory-hint'),
                         'hv-extcalls=on must not imply hv-memory-hint')

    def test_hv_memory_hint_on(self):
        """
        hv-memory-hint=on without hv-extcalls must still parse/boot (the
        combination is a no-op at runtime -- kvm_hv_handle_exit() gates
        *all* extended hypercalls, including the capabilities query, on
        hv-extcalls -- but QEMU must not reject it at CPU-realize time).
        """
        self.launch_with_cpu_props(',hv-memory-hint=on')
        self.assertFalse(self.get_cpu_prop('hv-extcalls'))
        self.assertTrue(self.get_cpu_prop('hv-memory-hint'),
                        'hv-memory-hint=on must be reflected on the CPU')

    def test_both_on(self):
        """
        The intended "fully enabled" configuration: both properties on.
        """
        self.launch_with_cpu_props(',hv-extcalls=on,hv-memory-hint=on')
        self.assertTrue(self.get_cpu_prop('hv-extcalls'))
        self.assertTrue(self.get_cpu_prop('hv-memory-hint'))


class HypervExtCallsModelExpansion(QemuSystemTest):
    """
    Confirm hv-extcalls/hv-memory-hint are discoverable via
    query-cpu-model-expansion, the mechanism management layers (e.g.
    libvirt) use to probe which CPU properties/features are supported
    before adding them to a domain definition.
    """

    def test(self):
        self.set_machine('none')
        self.vm.add_args('-S')
        self.vm.launch()

        model = {'name': 'qemu64',
                'props': {'hv-extcalls': True, 'hv-memory-hint': True}}
        e = self.vm.cmd('query-cpu-model-expansion', model=model,
                        type='full')
        props = e['model']['props']

        self.assertIn('hv-extcalls', props)
        self.assertIn('hv-memory-hint', props)
        self.assertTrue(props['hv-extcalls'])
        self.assertTrue(props['hv-memory-hint'])


if __name__ == '__main__':
    QemuSystemTest.main()
