#!/usr/bin/env python3
#
# Functional test that hot-plugs and hot-unplugs a virtio-blk-pci disk
# on the pseries machine type.
#
# Copyright (c) 2025 IBM Corporation
#
# SPDX-License-Identifier: GPL-2.0-or-later

import platform

from qemu_test import LinuxKernelTest, Asset
from qemu_test import exec_command_and_wait_for_pattern


class PseriesHotplugBlk(LinuxKernelTest):
    """
    Test virtio-blk-pci hotplug/unplug on the pseries machine type.

    A pre-built rootfs image (based on the buildroot image used by
    test_hotplug_cpu.py) is used as the guest OS.  The image includes
    rtas_errd from ppc64-diag, which is required on pseries to call the
    ibm,configure-connector RTAS function that makes a QMP-hotplugged
    PCI device visible to the kernel.

    ppc64-diag is not packaged in Alpine Linux, so the Alpine+apk
    approach used by test_hv.py cannot be used here.  Instead a
    self-contained rootfs with rtas_errd and drmgr (from powerpc-utils)
    already installed is referenced as an Asset, following the same
    pattern as test_hotplug_cpu.py.

    The test is run with both TCG and KVM; the KVM variant needs a
    ppc64 host with kvm-hv loaded and is skipped automatically otherwise.

    A second spapr-pci-host-bridge (phb1, index=1) is present at VM
    launch so the guest kernel registers its PCI slots with rpaphp
    during boot-time PCI probe.  Runtime DLPAR hotplug of
    virtio-blk-pci into phb1.0 then works via the rtas_errd path.
    """

    timeout = 600

    ASSET_KERNEL = Asset(
        ('https://github.com/legoater/qemu-ppc-boot/raw/refs/heads/main/'
         'buildroot/qemu_ppc64le_pseries-2023.11-8-gdcd9f0f6eb-20240104/'
         'vmlinux'),
        'fc20d5820ad83ed514ac5f88af7e2692d47ab7f36dab290cdb137d05b98502f5')

    # Pre-built rootfs based on the buildroot pseries image above, with
    # rtas_errd (from ppc64-diag) and drmgr (from powerpc-utils) added.
    # rtas_errd is required to process RTAS hotplug events on pseries and
    # it execs drmgr to configure the device; without them rpaphp
    # registers the slots but the device is never enumerated by the kernel.
    ASSET_ROOTFS = Asset(
        ('https://github.com/legoater/qemu-ppc-boot/raw/refs/heads/main/'
         'buildroot/qemu_ppc64le_pseries-2023.11-8-gdcd9f0f6eb-20240104/'
         'rootfs-hotplug.ext2'),
        '01ab3421d313e7765176eb27193fdb5837281dc3c8484eed140de8a3e65908eb')

    def _boot_pseries(self, accel):
        """Boot pseries with phb1 present and rtas_errd running.

        accel must be 'tcg' or 'kvm'.
        """
        kernel_path = self.ASSET_KERNEL.fetch()
        rootfs_path = self.ASSET_ROOTFS.fetch()

        self.set_machine('pseries')
        if accel == 'tcg':
            self.vm.add_args('-accel', 'tcg,thread=multi')
        else:
            self.vm.add_args('-accel', 'kvm')
        self.vm.add_args('-smp', '2', '-m', '512M')
        # Second PHB present at boot so the guest kernel registers its
        # slots with rpaphp.  Required for runtime DLPAR hotplug of
        # virtio-blk-pci into phb1.0.
        self.vm.add_args('-device', 'spapr-pci-host-bridge,id=phb1,index=1')
        # snapshot=on keeps the cached rootfs asset unmodified
        self.vm.add_args(
            '-drive',
            f'file={rootfs_path},if=scsi,index=0,format=raw,snapshot=on')
        self.vm.add_args(
            '-kernel', kernel_path,
            '-append', 'root=/dev/sda console=hvc0 rootwait printk.time=0',
        )
        self.vm.set_console()
        self.vm.launch()

        self.wait_for_console_pattern('buildroot login:')
        exec_command_and_wait_for_pattern(self, 'root', '#')

        # Start rtas_errd so that RTAS hotplug events are processed.
        # rtas_errd daemonizes by default (no -d flag).  Run mkdir and
        # rtas_errd unconditionally then echo a sentinel so the pattern
        # match never depends on their exit codes.
        exec_command_and_wait_for_pattern(
            self,
            'touch /var/log/platform; mkdir -p /var/log/dump;'
            ' /usr/sbin/rtas_errd;'
            ' echo rtas_errd_started',
            'rtas_errd_started',
        )

    def _do_blk_hotplug(self):

        # Confirm /dev/vda does not exist before hotplug.
        exec_command_and_wait_for_pattern(
            self,
            'test -b /dev/vda && echo present || echo absent',
            'absent',
        )

        # Add a null-co block backend (no image file needed).
        self.vm.cmd(
            'blockdev-add',
            **{
                'driver': 'null-co',
                'size': 1073741824,
                'node-name': 'testdisk',
            },
        )

        # Hotplug virtio-blk-pci into phb1.0.  The RTAS hotplug event
        # (type 229) is delivered to the guest; rtas_errd calls
        # ibm,configure-connector, the kernel enumerates the device and
        # udev creates /dev/vda.
        self.vm.cmd(
            'device_add',
            driver='virtio-blk-pci',
            drive='testdisk',
            id='hotblk0',
            bus='phb1.0',
            addr='0x0',
        )

        # Poll until /dev/vda appears.  Use a shell variable sentinel so
        # the string 'vda_present' never appears in the typed command,
        # avoiding a false match on the echoed command text.
        exec_command_and_wait_for_pattern(
            self,
            'until test -b /dev/vda; do sleep 0.2; done'
            '; R=present; echo vda_$R',
            'vda_present',
        )

        # Unplug the device and wait for the QMP deletion event.
        self.vm.cmd('device_del', id='hotblk0')
        self.vm.event_wait(
            'DEVICE_DELETED',
            match={'data': {'device': 'hotblk0'}},
        )

        # Poll until /dev/vda disappears.
        exec_command_and_wait_for_pattern(
            self,
            'while test -b /dev/vda; do sleep 0.2; done'
            '; R=absent; echo vda_$R',
            'vda_absent',
        )

    def test_pseries_hotplug_blk_tcg(self):
        """Hotplug then unplug a virtio-blk-pci device on pseries (TCG)."""
        self.require_accelerator('tcg')
        self._boot_pseries('tcg')
        self._do_blk_hotplug()

    def test_pseries_hotplug_blk_kvm(self):
        """Hotplug then unplug a virtio-blk-pci device on pseries (KVM).

        Requires a ppc64 host with kvm-hv loaded; skipped automatically
        on other hosts.
        """
        self.require_accelerator('kvm')
        # On ppc64 the base kvm module can be loaded (and /dev/kvm created)
        # without kvm-hv, so checking /dev/kvm alone is not sufficient.
        if platform.machine() in ('ppc64le', 'ppc64'):
            try:
                with open('/proc/modules', encoding='utf-8') as f:
                    if not any(line.split()[0] == 'kvm_hv' for line in f):
                        self.skipTest('kvm-hv module is not loaded')
            except OSError:
                pass
        self._boot_pseries('kvm')
        self._do_blk_hotplug()


if __name__ == '__main__':
    PseriesHotplugBlk.main()
