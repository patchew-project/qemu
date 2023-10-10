# Functional tests that boot FreeBSD in various configurations
#
# Copyright (c) 2023 IBM Corporation
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

import os

from avocado import skipUnless
from avocado import skipIf
from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern
from avocado_qemu import exec_command
from avocado.utils import archive
from avocado.utils import process
from avocado.utils.path import find_command

class BootFreeBSDPPC64(QemuSystemTest):
    """
    :avocado: tags=arch:ppc64
    """

    timeout = 360

    def run_pseries_test(self, force_HPT=False):
        # We need zstd for all the tuxrun tests
        # See https://github.com/avocado-framework/avocado/issues/5609
        zstd = find_command('zstd', False)
        if zstd is False:
            self.cancel('Could not find "zstd", which is required to '
                        'decompress rootfs')
        self.zstd = zstd

        drive_url = ('https://artifact.ci.freebsd.org/snapshot/15.0-CURRENT/a2440348eed75bb7682579af0905b652747fd016/powerpc/powerpc64le/disk.qcow2.zst')
        drive_hash = '8ab11a05ccab3d44215fd4667a70454ed10a203f'
        drive_path_zstd = self.fetch_asset(drive_url, asset_hash=drive_hash)
        drive_path = os.path.join(self.workdir, 'disk.qcow2')
        # archive.zstd_uncompress(drive_path_zstd, drive_path)

        cmd = f"{self.zstd} -d {drive_path_zstd} -o {drive_path}"
        process.run(cmd)

        drive = f"file={drive_path},format=qcow2,if=virtio"

        self.vm.set_console()
        if force_HPT:
            self.vm.add_args('-m', '4g')
        else:
            self.vm.add_args('-m', '1g')
        self.vm.add_args('-smp', '4')
        self.vm.add_args('-drive', drive)
        self.vm.add_args('-net', 'nic,model=virtio')
        self.vm.launch()

        wait_for_console_pattern(self, 'Hit [Enter] to boot immediately, or any other key for command prompt.')
        if force_HPT:
            exec_command(self, 'x')
            wait_for_console_pattern(self, 'OK')
            exec_command(self, 'set radix_mmu=0')
            exec_command(self, 'boot')
            wait_for_console_pattern(self, 'cas: selected hash MMU', 'panic:')
        else:
            exec_command(self, '')
            wait_for_console_pattern(self, 'cas: selected radix MMU')

        wait_for_console_pattern(self, 'FreeBSD 15.0-CURRENT #0 a244034: Mon Sep 25 02:05:22 UTC 2023', 'panic:')
        wait_for_console_pattern(self, 'FreeBSD/SMP: Multiprocessor System Detected: 4 CPUs')
        wait_for_console_pattern(self, 'FreeBSD/powerpc (Amnesiac) (ttyu0)', 'panic:')

    @skipIf(os.getenv('GITLAB_CI'), 'Running on GitLab')
    def test_pseries_tcg(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        :avocado: tags=accel:tcg
        """
        self.require_accelerator("tcg")
        self.run_pseries_test()

    @skipIf(os.getenv('GITLAB_CI'), 'Running on GitLab')
    def test_pseries_hpt_tcg(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        :avocado: tags=accel:tcg
        """
        self.require_accelerator("tcg")
        self.run_pseries_test(force_HPT=True)

    @skipIf(os.getenv('GITLAB_CI'), 'Running on GitLab')
    def test_pseries_kvm(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        :avocado: tags=accel:kvm
        """
        self.require_accelerator("kvm")
        self.run_pseries_test()

    @skipIf(os.getenv('GITLAB_CI'), 'Running on GitLab')
    def test_pseries_hpt_kvm(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        :avocado: tags=accel:kvm
        """
        self.require_accelerator("kvm")
        self.run_pseries_test(force_HPT=True)
