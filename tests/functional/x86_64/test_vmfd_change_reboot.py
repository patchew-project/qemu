#!/usr/bin/env python3
#
# KVM VM file descriptor change on reset test
#
# Copyright © 2026 Red Hat, Inc.
#
# Author:
#  Ani Sinha <anisinha@redhat.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
from qemu.machine import machine

from qemu_test import QemuSystemTest, Asset, exec_command_and_wait_for_pattern
from qemu_test import wait_for_console_pattern

class KVMGuest(QemuSystemTest):

    # ASSET UKI was generated using
    # https://gitlab.com/kraxel/edk2-tests/-/blob/unittest/tools/make-supermin.sh
    ASSET_UKI = Asset('https://gitlab.com/anisinha/misc-artifacts/'
                      '-/raw/main/uki.x86-64.efi?ref_type=heads',
                      'e0f806bd1fa24111312e1fe849d2ee69808d4343930a5'
                      'dc8c1688da17c65f576')
    # ASSET_OVMF comes from /usr/share/edk2/ovmf/OVMF.stateless.fd of a fc43
    # distro which in turn comes from the edk2-ovmf-20251119-3.fc43.noarch rpm.
    ASSET_OVMF = Asset('https://gitlab.com/anisinha/misc-artifacts/'
                       '-/raw/main/OVMF.stateless.fd?ref_type=heads',
                       '58a4275aafa8774bd6b1540adceae4ea434b8db75b476'
                       '11839ff47be88cfcf22')

    def common_vm_setup(self, kvm_args=None, cpu_args=None):
        self.require_accelerator("kvm")

        self.vm.set_console()
        if kvm_args:
            self.vm.add_args("-accel", "kvm,%s" %kvm_args)
        else:
            self.vm.add_args("-accel", "kvm")
        self.vm.add_args("-smp", "2")
        if cpu_args:
            self.vm.add_args("-cpu", "host,%s" %cpu_args)
        else:
            self.vm.add_args("-cpu", "host")
        self.vm.add_args("-m", "2G")
        self.vm.add_args("-nographic", "-nodefaults")

        self.uki_path = self.ASSET_UKI.fetch()
        self.ovmf_path = self.ASSET_OVMF.fetch()

    def run_and_check(self):
        self.vm.add_args('-kernel', self.uki_path)
        self.vm.add_args("-bios", self.ovmf_path)
        # enable KVM VMFD change on reset for a non-coco VM
        self.vm.add_args("-machine", "q35,x-change-vmfd-on-reset=on")
        # enable tracing
        self.vm.add_args("-d", "trace:kvm_reset_vmfd")

        try:
            self.vm.launch()
        except machine.VMLaunchFailure as e:
            if "Xen HVM guest support not present" in e.output:
                self.skipTest("KVM Xen support is not present "
                              "(need v5.12+ kernel with CONFIG_KVM_XEN)")
            elif "Property 'kvm-accel.xen-version' not found" in e.output:
                self.skipTest("QEMU not built with CONFIG_XEN_EMU support")
            else:
                raise e

        self.log.info('VM launched')
        console_pattern = 'bash-5.1#'
        wait_for_console_pattern(self, console_pattern)
        self.log.info('VM ready with a bash prompt')

        exec_command_and_wait_for_pattern(self, '/usr/sbin/reboot -f',
                                          'reboot: machine restart')
        console_pattern = '# --- Hello world ---'
        wait_for_console_pattern(self, console_pattern)
        self.vm.shutdown()
        self.assertRegex(self.vm.get_log(),
                         r'kvm_reset_vmfd')
        self.assertRegex(self.vm.get_log(),
                         r'virtual machine accel file descriptor has changed')

    def test_vmfd_change_on_reset(self):
        self.common_vm_setup()
        self.run_and_check()

    def test_xen_emulation(self):
        self.common_vm_setup("xen-version=0x4000a,kernel-irqchip=split")
        self.run_and_check()


if __name__ == '__main__':
    QemuSystemTest.main()
