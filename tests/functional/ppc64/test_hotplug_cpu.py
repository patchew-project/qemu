#!/usr/bin/env python3
# Copyright (c) 2025 IBM Corporation
#
# SPDX-License-Identifier: GPL-2.0-or-later

import platform

from qemu_test import LinuxKernelTest, Asset
from qemu_test import exec_command_and_wait_for_pattern


class PseriesCpuHotplug(LinuxKernelTest):
    """
    Test DLPAR CPU hotplug/unplug on the pseries machine type.

    The pseries machine implements the PAPR CPU hotplug protocol: the
    hypervisor presents Dynamic Reconfiguration Connectors (DRCs) to
    the guest, and the guest uses RTAS calls to accept or reject CPUs
    that are offered or withdrawn.

    Topology used: -smp 2,threads=2,cores=1,maxcpus=4 — 1 active core
    (cpu0+cpu1 online), max 2 cores.  Hot-add core-id=2 → online goes
    0-3; hot-remove → back to 0-1.  Verified via /sys/.../cpu/online.

    Two variants are provided:
      * test_pseries_cpu_hotplug_tcg — uses TCG, runs on any host arch.
      * test_pseries_cpu_hotplug_kvm — uses KVM, requires a ppc64le host
        with kvm-hv loaded; skipped automatically on other hosts.
    """

    timeout = 480

    # Buildroot image built for pseries (ppc64le little-endian paravirtual).
    ASSET_KERNEL = Asset(
        ('https://github.com/legoater/qemu-ppc-boot/raw/refs/heads/main/'
         'buildroot/qemu_ppc64le_pseries-2023.11-8-gdcd9f0f6eb-20240104/vmlinux'),
        'fc20d5820ad83ed514ac5f88af7e2692d47ab7f36dab290cdb137d05b98502f5')

    ASSET_ROOTFS = Asset(
        ('https://github.com/legoater/qemu-ppc-boot/raw/refs/heads/main/'
         'buildroot/qemu_ppc64le_pseries-2023.11-8-gdcd9f0f6eb-20240104/rootfs.ext2'),
        '6fd31e2300e9921d1192aa5c997760fc8bc40a18513cc466c56902f72d61fd23')

    def _boot_pseries(self, accel):
        """
        Boot pseries with 1 core present and room for a second core.

        -smp 2,threads=2,cores=1,maxcpus=4 gives us:
          * 1 active core  → cpu0 + cpu1 (2 SMT threads)
          * 1 spare core slot that can be filled by device_add

        accel must be 'tcg' or 'kvm'.
        """
        kernel_path = self.ASSET_KERNEL.fetch()
        rootfs_path = self.ASSET_ROOTFS.fetch()

        self.set_machine('pseries')
        # Do NOT use x-vof=on here — VOF skips the SLOF firmware path that
        # sets up the RTAS event interrupt (epow/check-exception).  Without
        # SLOF, the guest kernel's pseries_hp_wq workqueue is never triggered
        # and CPU hotplug RTAS events are silently dropped.
        if accel == 'tcg':
            self.vm.add_args('-accel', 'tcg,thread=multi')
        else:
            self.vm.add_args('-accel', 'kvm')
        self.vm.add_args('-smp', '2,threads=2,cores=1,maxcpus=4')
        self.vm.add_args('-m', '512M')
        self.vm.set_console()
        self.vm.add_args('-drive',
                         f'file={rootfs_path},if=scsi,index=0,format=raw')
        self.vm.add_args(
            '-kernel', kernel_path,
            '-append', 'root=/dev/sda console=hvc0 rootwait printk.time=0',
        )
        self.vm.launch()
        self.wait_for_console_pattern('buildroot login:')
        exec_command_and_wait_for_pattern(self, 'root', '#')

    def _get_hotplug_cpu_driver(self):
        """
        Return the spapr-cpu-core driver name for an empty hotplug slot.

        query-hotpluggable-cpus lists all possible CPU slots; entries
        without a 'qom-path' key are currently empty.  The 'type' field
        of any empty slot gives the driver name to pass to device_add.
        This avoids hard-coding a CPU version string (e.g.
        'power11_v2.0-spapr-cpu-core') that would break when the default
        pseries CPU changes in a future QEMU release.
        """
        cpus = self.vm.cmd('query-hotpluggable-cpus')
        for entry in cpus:
            if 'qom-path' not in entry:
                return entry['type']
        self.fail('No empty hotpluggable CPU slot found')

    def _do_cpu_hotplug(self):
        """
        Shared hotplug/unplug logic, called after the guest has booted.

        Confirms cpu0+cpu1 are online, hot-adds a second core (cpu2+cpu3),
        verifies they come online, hot-removes the core, and verifies they
        go offline.  Finishes with a clean guest poweroff.
        """
        # Confirm initial state: cpu0 and cpu1 online, cpu2 offline/absent.
        # Use /sys/devices/system/cpu/online which lists only truly online
        # CPUs — sysfs cpu dirs exist for possible CPUs too so ls is wrong.
        exec_command_and_wait_for_pattern(
            self,
            'cat /sys/devices/system/cpu/online',
            '0-1',
        )

        # Detect the correct driver for the empty hotplug slot dynamically
        # so the test works regardless of which CPU version pseries defaults
        # to (power10, power11, etc.).
        driver = self._get_hotplug_cpu_driver()

        # Hot-add core at core-id=2 (supplies threads cpu2 and cpu3 in SMT-2).
        # With threads=2 the available hotplug slot is core-id=2.
        self.vm.cmd(
            'device_add',
            driver=driver,
            id='hotcpu1',
            **{'core-id': 2},
        )

        # Poll until cpu2+cpu3 are online.  The sentinel is built using a
        # shell variable (echo hotplug_cpu_$R) so the full string
        # 'hotplug_cpu_done' never appears in the command text.  This avoids
        # a false match on the shell's echo of the typed command, which
        # exec_command_and_wait_for_pattern scans before any output arrives.
        exec_command_and_wait_for_pattern(
            self,
            'until [ "$(cat /sys/devices/system/cpu/online)" = "0-3" ];'
            ' do sleep 0.5; done; R=done; echo hotplug_cpu_$R',
            'hotplug_cpu_done',
        )

        # Hot-remove the core.
        self.vm.cmd('device_del', id='hotcpu1')

        # Wait until the kernel acknowledges the removal.
        exec_command_and_wait_for_pattern(
            self,
            'until [ "$(cat /sys/devices/system/cpu/online)" = "0-1" ];'
            ' do sleep 0.2; done; R=done; echo unplug_cpu_$R',
            'unplug_cpu_done',
        )

        exec_command_and_wait_for_pattern(self, 'poweroff', '#')
        self.vm.wait()

    def test_pseries_cpu_hotplug_tcg(self):
        """Hot-add then hot-remove a CPU core on pseries using TCG."""
        self.require_accelerator('tcg')
        self._boot_pseries('tcg')
        self._do_cpu_hotplug()

    def test_pseries_cpu_hotplug_kvm(self):
        """Hot-add then hot-remove a CPU core on pseries using KVM.

        Requires a ppc64le host with kvm-hv loaded; skipped automatically
        on other hosts.
        """
        self.require_accelerator('kvm')
        # On ppc64le the base kvm module can be loaded (and /dev/kvm created)
        # without kvm-hv, so checking /dev/kvm alone is not sufficient.
        # Verify that kvm_hv is actually present in /proc/modules.
        # This check is ppc64-specific; other arches (x86, arm, ...) use
        # different KVM modules and rely solely on /dev/kvm.
        if platform.machine() in ('ppc64le', 'ppc64'):
            try:
                with open('/proc/modules', encoding='utf-8') as f:
                    if not any(line.split()[0] == 'kvm_hv' for line in f):
                        self.skipTest('kvm-hv module is not loaded')
            except OSError:
                pass
        self._boot_pseries('kvm')
        self._do_cpu_hotplug()


if __name__ == '__main__':
    PseriesCpuHotplug.main()
