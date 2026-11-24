#!/usr/bin/env python3
# Copyright (c) 2025 IBM Corporation
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import QemuSystemTest


class PseriesCapsTest(QemuSystemTest):
    """
    Quick (no-boot) tests for pseries machine capabilities.

    Each test starts QEMU with -S (halted) so no firmware or kernel is
    needed.  The tests exercise the QMP interface to verify that:

      * The machine type is visible in 'query-machines' output.
      * Known security capability properties (cap-cfpc, cap-sbbc,
        cap-ibs, cap-ccf-assist, cap-nested-hv) are accepted and read
        back with the values that were set.
      * Versioned compat machines (pseries-7.2, pseries-9.2 and
        pseries-10.0) can be instantiated and are not marked
        is-default.
      * cap-hpt-max-page-size is reachable via the QOM tree.
    """

    timeout = 30

    # ------------------------------------------------------------------ #
    # Helpers
    # ------------------------------------------------------------------ #

    def _launch_stopped(self, machine='pseries', extra_args=None):
        """Launch QEMU paused (-S) for the named machine and return."""
        self.set_machine(machine)
        self.vm.add_args('-S', '-nodefaults')
        if extra_args:
            self.vm.add_args(*extra_args)
        self.vm.launch()

    def _query_machine_info(self, name):
        """Return the entry from query-machines that matches *name* or alias."""
        machines = self.vm.cmd('query-machines')
        for m in machines:
            if m['name'] == name or m.get('alias') == name:
                return m
        return None

    # ------------------------------------------------------------------ #
    # Test: basic pseries machine is visible in query-machines
    # ------------------------------------------------------------------ #

    def test_pseries_in_query_machines(self):
        """pseries (latest) must appear in query-machines output."""
        self._launch_stopped('pseries')
        info = self._query_machine_info('pseries')
        self.assertIsNotNone(info,
                             'pseries not found in query-machines output')
        # The default/latest versioned machine carries is-default=True.
        self.assertTrue(
            info.get('is-default', False),
            'pseries alias should be marked is-default=true',
        )

    # ------------------------------------------------------------------ #
    # Test: security cap overrides are accepted without error
    # ------------------------------------------------------------------ #

    def test_pseries_security_caps_override(self):
        """
        Verify pseries accepts all well-known security cap overrides.

        These are the options used by the KVM nested-HV path in
        test_hv.py.  Check that QEMU starts cleanly and that each
        capability reads back through qom-get with the value that was
        set on the command line.
        """
        caps = (
            'cap-cfpc=broken,'
            'cap-sbbc=broken,'
            'cap-ibs=broken,'
            'cap-ccf-assist=off'
        )
        self._launch_stopped('pseries', extra_args=['-machine', caps])
        for name in ('cap-cfpc', 'cap-sbbc', 'cap-ibs'):
            self.assertEqual(
                self.vm.cmd('qom-get', path='/machine', property=name),
                'broken',
                f'{name} should read back as broken',
            )
        self.assertFalse(
            self.vm.cmd('qom-get', path='/machine',
                        property='cap-ccf-assist'),
            'cap-ccf-assist should read back as off',
        )

    # ------------------------------------------------------------------ #
    # Test: nested-HV cap is accepted
    # ------------------------------------------------------------------ #

    def test_pseries_nested_hv_cap(self):
        """pseries must accept cap-nested-hv=on and report it as on."""
        self._launch_stopped(
            'pseries',
            extra_args=['-machine', 'x-vof=on,cap-nested-hv=on'],
        )
        self.assertTrue(
            self.vm.cmd('qom-get', path='/machine',
                        property='cap-nested-hv'),
            'cap-nested-hv should read back as on',
        )

    # ------------------------------------------------------------------ #
    # Test: versioned compat machines can be instantiated
    # ------------------------------------------------------------------ #

    def _test_compat_machine(self, version_str):
        """
        Helper: launch a versioned compat machine and verify that the
        name appears in query-machines and that it is not marked
        is-default (the default is always the latest version).
        """
        self._launch_stopped(version_str)
        info = self._query_machine_info(version_str)
        self.assertIsNotNone(
            info,
            f'compat machine {version_str!r} not found in query-machines',
        )
        # query-machines only reports is-default for the default (latest)
        # machine, so it must be absent or False for a compat machine.
        self.assertFalse(
            info.get('is-default', False),
            f'{version_str} should not be marked is-default=true',
        )

    def test_pseries_compat_7_2(self):
        """pseries-7.2 (vmstate reference version) can be instantiated."""
        self._test_compat_machine('pseries-7.2')

    def test_pseries_compat_9_2(self):
        """pseries-9.2 can be instantiated."""
        self._test_compat_machine('pseries-9.2')

    def test_pseries_compat_10_0(self):
        """pseries-10.0 can be instantiated."""
        self._test_compat_machine('pseries-10.0')

    # ------------------------------------------------------------------ #
    # Test: cap-hpt-max-page-size is reachable via the QOM tree
    # ------------------------------------------------------------------ #

    def test_pseries_cap_hpt_max_page_size(self):
        """
        Verify that pseries exposes cap-hpt-max-page-size in its
        machine properties (it is used by migration compat checks).
        """
        self._launch_stopped('pseries')
        # query-machine-props is not a real QMP command; we use the
        # object model instead.
        props = self.vm.cmd('qom-list', path='/machine')
        prop_names = [p['name'] for p in props]
        # The property should be reachable via the QOM tree.
        self.assertIn(
            'cap-hpt-max-page-size', prop_names,
            'pseries /machine must expose cap-hpt-max-page-size via QOM',
        )


if __name__ == '__main__':
    PseriesCapsTest.main()
