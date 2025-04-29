#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# This test runs the vmstate-static-checker script with the current QEMU

import subprocess

from qemu_test import QemuSystemTest


class VmStateTest(QemuSystemTest):

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
        vmstate_checker = self.data_file('..', '..', 'scripts',
                                         'vmstate-static-checker.py')
        self.log.info('Comparing vmstate with ' + src_json)
        cp = subprocess.run([vmstate_checker, '-s', src_json, '-d', dst_json],
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT,
                            text=True)
        if cp.returncode != 0:
            self.fail('Running vmstate-static-checker failed:\n' + cp.stdout)
        if cp.stdout:
            self.log.warning('vmstate-static-checker output: ' + cp.stdout)


if __name__ == '__main__':
    QemuSystemTest.main()
