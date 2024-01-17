# Functional tests that boot FreeBSD in various configurations
#
# Copyright (c) 2023 IBM Corporation
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

import os
import subprocess

from avocado import skipUnless
from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern
from avocado_qemu import exec_command
from avocado.utils import archive
from avocado.utils import process
from avocado.utils.path import find_command

@skipUnless(os.getenv('AVOCADO_ALLOW_LARGE_STORAGE'), 'storage limited')
@skipUnless(os.getenv('SPEED') == 'slow', 'runtime limited')
class BootFreeBSDPPC64(QemuSystemTest):
    """
    :avocado: tags=arch:ppc64
    """

    timeout = 360

    def setUp(self):
        super().setUp()

        # We need zstd for all the tests
        # See https://github.com/avocado-framework/avocado/issues/5609
        zstd = find_command('zstd', False)
        if zstd is False:
            self.cancel('Could not find "zstd", which is required to '
                        'decompress rootfs')
        tar = find_command('tar', False)
        if tar is False:
            self.cancel('Could not find "tar", which is required to '
                        'decompress rootfs')

        drive_url = ('https://artifact.ci.freebsd.org/snapshot/15.0-CURRENT/8a735ffdf04936c6785ac4fa31486639262dd416/powerpc/powerpc64le/disk.qcow2.zst')
        drive_hash = '95d863dbbc4b60f4899d1ef21d6489fca05bf03d'
        drive_path_zstd = self.fetch_asset(drive_url, asset_hash=drive_hash)
        self.drive_path = os.path.join(self.workdir, 'disk.qcow2')

        cmd = f"{zstd} -d {drive_path_zstd} -o {self.drive_path}"
        process.run(cmd)

        kernel_url = ('https://artifact.ci.freebsd.org/snapshot/15.0-CURRENT/8a735ffdf04936c6785ac4fa31486639262dd416/powerpc/powerpc64le/kernel.txz')
        kernel_hash = '31d14c2dc658858830a7acab5128a5b91ea548cf'
        kernel_path_txz = self.fetch_asset(kernel_url, asset_hash=kernel_hash)
        self.kernel_path = os.path.join(self.workdir, 'kernel')

        with open(self.kernel_path, "w") as outfile:
            cmd = f"{tar} OJxf {kernel_path_txz} ./boot/kernel/kernel"
            subprocess.run(cmd.split(), stdout=outfile)

    def set_pseries_devices(self):
        self.vm.add_args('-drive', f"file={self.drive_path},format=qcow2,if=virtio")
        self.vm.add_args('-net', 'nic,model=virtio')

    def set_powernv_devices(self):
        self.vm.add_args('-device', 'nvme,bus=pcie.2,addr=0x0,serial=1234,drive=drive0',
                         '-device', 'e1000e,netdev=net0,mac=C0:FF:EE:00:00:02,bus=pcie.0,addr=0x0',
                         '-netdev', 'user,id=net0,hostfwd=::20022-:22,hostname=freebsd')
        self.vm.add_args("-drive", f"file={self.drive_path},format=qcow2,if=none,id=drive0")
        self.vm.add_args("-kernel", self.kernel_path)

    def run_pseries_test(self, force_HPT=False):
        if force_HPT:
            self.vm.add_args('-m', '4g')
        else:
            self.vm.add_args('-m', '1g')
        self.vm.add_args('-smp', '4')
        self.set_pseries_devices()
        self.vm.set_console()
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
            wait_for_console_pattern(self, 'cas: selected radix MMU', 'panic:')

        wait_for_console_pattern(self, 'FreeBSD 15.0-CURRENT', 'panic:')
        wait_for_console_pattern(self, 'FreeBSD/SMP: Multiprocessor System Detected: 4 CPUs', 'panic:')
        wait_for_console_pattern(self, 'FreeBSD/powerpc (Amnesiac) (ttyu0)', 'panic:')
        exec_command(self, 'root')
        wait_for_console_pattern(self, 'resizewin: timeout reading from terminal')
        exec_command(self, 'poweroff')
        wait_for_console_pattern(self, 'Uptime:', 'panic:')
        self.vm.wait()

    # powernv does not have a bootloader so must load the kernel directly
    # and work around manual specification of the root device. Also can't
    # specify options like radix_mmu the same way (todo: work out how it
    # is done, and add HPT test).
    def run_powernv_test(self, force_HPT=False):
        self.vm.add_args('-m', '8g')
        self.vm.add_args('-smp', '4')
        self.set_powernv_devices()
        self.vm.set_console()
        self.vm.launch()

        wait_for_console_pattern(self, 'FreeBSD 15.0-CURRENT', 'panic:')
        wait_for_console_pattern(self, 'FreeBSD/SMP: Multiprocessor System Detected: 4 CPUs', 'panic:')
        wait_for_console_pattern(self, '<empty line>    Abort manual input')
        exec_command(self, 'ufs:diskid/DISK-1234s3')

        wait_for_console_pattern(self, 'FreeBSD/powerpc (Amnesiac) (ttyu0)', 'panic:')
        exec_command(self, 'root')
        wait_for_console_pattern(self, 'resizewin: timeout reading from terminal')
        exec_command(self, 'poweroff')
        wait_for_console_pattern(self, 'Uptime:', 'panic:')
        wait_for_console_pattern(self, 'OPAL: Shutdown request', 'panic:')
        self.vm.wait()

    def test_pseries_tcg(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        :avocado: tags=accel:tcg
        """
        self.require_accelerator("tcg")
        self.vm.add_args("-cpu", "power10")
        self.vm.add_args("-accel", "tcg,thread=multi")
        self.run_pseries_test()

    def test_pseries_hpt_tcg(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        :avocado: tags=accel:tcg
        """
        self.require_accelerator("tcg")
        self.vm.add_args("-accel", "tcg,thread=multi")
        self.run_pseries_test(force_HPT=True)

    def test_pseries_kvm(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        :avocado: tags=accel:kvm
        """
        self.require_accelerator("kvm")
        self.vm.add_args("-accel", "kvm")
        self.run_pseries_test()

    def test_pseries_hpt_kvm(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        :avocado: tags=accel:kvm
        """
        self.require_accelerator("kvm")
        self.vm.add_args("-accel", "kvm")
        self.run_pseries_test(force_HPT=True)

    # powernv9 works, powernv10 fails
    def test_powernv(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:powernv9
        :avocado: tags=accel:tcg
        """
        self.require_accelerator("tcg")
        self.vm.add_args("-accel", "tcg,thread=multi")
        self.run_powernv_test()
