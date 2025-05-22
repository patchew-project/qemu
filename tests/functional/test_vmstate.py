#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# This test runs the vmstate-static-checker script with the current QEMU

import subprocess

from qemu_test import QemuSystemTest


expected_output='''Warning: checking incompatible machine types: "pc-i440fx-2.1", "pc-i440fx-2.2"
Section "fw_cfg" does not exist in dest
Section "fusbh200-ehci-usb" version error: 2 > 1
Section "fusbh200-ehci-usb", Description "ehci-core": expected field "usbsts", got "usbsts_pending"; skipping rest
Section "pci-serial-4x" Description "pci-serial-multi": Entry "Fields" missing
Section "intel-hda-generic", Description "intel-hda", Field "pci": missing description
Section "cfi.pflash01": Entry "Description" missing
Section "megasas", Description "PCIDevice": expected field "irq_state", while dest has no further fields
Section "PIIX3-xen" Description "PIIX3": minimum version error: 1 < 2
Section "PIIX3-xen" Description "PIIX3": Entry "Subsections" missing
Section "tpci200": Description "tpci200" missing, got "tpci2002" instead; skipping
Section "sun-fdtwo" Description "fdc": version error: 2 > 1
Section "sun-fdtwo", Description "fdrive": Subsection "fdrive/media_rate" not found
Section "usb-kbd" Description "usb-kbd" Field "kbd.keycodes" size mismatch: 4 , 2
'''

class VmStateTest(QemuSystemTest):

    def run_vmstate_checker(self, src_json, dst_json):
        checkerscript = self.data_file('..', '..', 'scripts',
                                       'vmstate-static-checker.py')
        return subprocess.run([checkerscript, '-s', src_json, '-d', dst_json],
                              stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT,
                              text=True)

    def test_checker(self):
        """
        Test whether the checker script correctly detects the changes
        between dump1.json and dump2.json.
        """
        if self.arch != 'x86_64':
            self.skipTest('for x86 only')
        src_json = self.data_file('..', 'data', 'vmstate-static-checker',
                                  'dump1.json')
        dst_json = self.data_file('..', 'data', 'vmstate-static-checker',
                                  'dump2.json')
        self.log.info(f'Comparing {src_json} with {dst_json}')
        cp = self.run_vmstate_checker(src_json, dst_json)
        if cp.returncode != 13:
            self.fail('Unexpected return code of vmstate-static-checker: ' +
                      cp.returncode)
        if cp.stdout != expected_output:
            self.log.info('vmstate-static-checker output:\n' + cp.stdout)
            self.log.info('expected output:\n' + expected_output)
            self.fail('Unexpected vmstate-static-checker output!')

    def test_vmstate(self):
        target_machine = {
            'aarch64': 'virt-7.2',
            'm68k': 'virt-7.2',
            'ppc64': 'pseries-7.2',
            's390x': 's390-ccw-virtio-7.2',
            'x86_64': 'pc-q35-7.2',
        }
        self.set_machine(target_machine[self.arch])

        # Run QEMU to get the current vmstate json file:
        dst_json = self.scratch_file('dest.json')
        self.log.info('Dumping vmstate from ' + self.qemu_bin)
        cp = subprocess.run([self.qemu_bin, '-nodefaults',
                             '-M', target_machine[self.arch],
                             '-dump-vmstate', dst_json],
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT,
                            text=True)
        if cp.returncode != 0:
            self.fail('Running QEMU failed:\n' + cp.stdout)
        if cp.stdout:
            self.log.info('QEMU output: ' + cp.stdout)

        # Check whether the old vmstate json file is still compatible:
        src_json = self.data_file('..', 'data', 'vmstate-static-checker',
                                  self.arch,
                                  target_machine[self.arch] + '.json')
        self.log.info('Comparing vmstate with ' + src_json)
        cp = self.run_vmstate_checker(src_json, dst_json)
        if cp.returncode != 0:
            self.fail('Running vmstate-static-checker failed:\n' + cp.stdout)
        if cp.stdout:
            self.log.warning('vmstate-static-checker output: ' + cp.stdout)


if __name__ == '__main__':
    QemuSystemTest.main()
