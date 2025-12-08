#!/usr/bin/env python3
#
# s390x Secure IPL functional test: validates secure-boot verification results
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import time

from qemu_test import QemuSystemTest, Asset
from qemu_test import exec_command_and_wait_for_pattern, exec_command
from qemu_test import wait_for_console_pattern, skipBigDataTest
from qemu.utils import kvm_available, tcg_available

class S390xSecureIpl(QemuSystemTest):
    ASSET_F40_QCOW2 = Asset(
        ('https://archives.fedoraproject.org/pub/archive/'
         'fedora-secondary/releases/40/Server/s390x/images/'
         'Fedora-Server-KVM-40-1.14.s390x.qcow2'),
        '091c232a7301be14e19c76ce9a0c1cbd2be2c4157884a731e1fc4f89e7455a5f')

    # Boot a temporary VM to set up secure IPL image:
    # - Create certificate
    # - Sign stage3 binary and kernel
    # - Run zipl
    # - Extract certificate
    # Small delay added to allow the guest prompt/filesystem updates to settle
    def setup_s390x_secure_ipl(self):
        temp_vm = self.get_vm(name='sipl_setup')
        temp_vm.set_machine('s390-ccw-virtio')
        self.require_accelerator('kvm')

        self.qcow2_path = self.ASSET_F40_QCOW2.fetch()

        temp_vm.set_console()
        temp_vm.add_args('-nographic',
                         '-accel', 'kvm',
                         '-m', '1024',
                         '-drive',
                         f'id=drive0,if=none,format=qcow2,file={self.qcow2_path}',
                         '-device', 'virtio-blk-ccw,drive=drive0,bootindex=1')
        temp_vm.launch()

        # Initial root account setup (Fedora first boot screen)
        self.root_password = 'fedora40password'
        wait_for_console_pattern(self, 'Please make a selection from the above',
                                 vm=temp_vm)
        exec_command_and_wait_for_pattern(self, '4', 'Password:', vm=temp_vm)
        exec_command_and_wait_for_pattern(self, self.root_password,
                                          'Password (confirm):', vm=temp_vm)
        exec_command_and_wait_for_pattern(self, self.root_password,
                                    'Please make a selection from the above',
                                    vm=temp_vm)

        # Login as root
        exec_command_and_wait_for_pattern(self, 'c', 'localhost login:', vm=temp_vm)
        exec_command_and_wait_for_pattern(self, 'root', 'Password:', vm=temp_vm)
        exec_command_and_wait_for_pattern(self, self.root_password,
                                          '[root@localhost ~]#', vm=temp_vm)

        # Certificate generation
        time.sleep(1)
        exec_command_and_wait_for_pattern(self,
                                         'openssl version', 'OpenSSL 3.2.1 30',
                                         vm=temp_vm)
        exec_command_and_wait_for_pattern(self,
                            'openssl req -new -x509 -newkey rsa:2048 '
                            '-keyout mykey.pem -outform PEM -out mycert.pem '
                            '-days 36500 -subj "/CN=My Name/" -nodes -verbose',
                            'Writing private key to \'mykey.pem\'', vm=temp_vm)

        # Install kernel-devel (needed for sign-file)
        exec_command_and_wait_for_pattern(self,
                                'sudo dnf install kernel-devel-$(uname -r) -y',
                                'Complete!', vm=temp_vm)
        time.sleep(1)
        exec_command_and_wait_for_pattern(self,
                                    'ls /usr/src/kernels/$(uname -r)/scripts/',
                                    'sign-file', vm=temp_vm)

        # Sign stage3 binary and kernel
        exec_command(self, '/usr/src/kernels/$(uname -r)/scripts/sign-file '
                    'sha256 mykey.pem mycert.pem /lib/s390-tools/stage3.bin',
                    vm=temp_vm)
        time.sleep(1)
        exec_command(self, '/usr/src/kernels/$(uname -r)/scripts/sign-file '
                    'sha256 mykey.pem mycert.pem /boot/vmlinuz-$(uname -r)',
                    vm=temp_vm)
        time.sleep(1)

        # Run zipl to prepare for secure boot
        exec_command_and_wait_for_pattern(self, 'zipl --secure 1 -VV', 'Done.',
                                          vm=temp_vm)

        # Extract certificate to host
        out = exec_command_and_wait_for_pattern(self, 'cat mycert.pem',
                                                '-----END CERTIFICATE-----',
                                                vm=temp_vm)
        # strip first line to avoid console echo artifacts
        cert = "\n".join(out.decode("utf-8").splitlines()[1:])
        self.log.info("%s", cert)

        self.cert_path = self.scratch_file("mycert.pem")

        with open(self.cert_path, 'w') as file_object:
            file_object.write(cert)

        # Shutdown temp vm
        temp_vm.shutdown()

    @skipBigDataTest()
    def test_s390x_secure_ipl(self):
        self.setup_s390x_secure_ipl()

        self.set_machine('s390-ccw-virtio')

        self.vm.set_console()
        self.vm.add_args('-nographic',
                         '-machine', 's390-ccw-virtio,secure-boot=on,'
                         f'boot-certs.0.path={self.cert_path}',
                         '-accel', 'kvm',
                         '-m', '1024',
                         '-drive',
                         f'id=drive1,if=none,format=qcow2,file={self.qcow2_path}',
                         '-device', 'virtio-blk-ccw,drive=drive1,bootindex=1')
        self.vm.launch()

        # Expect two verified components
        verified_output = "Verified component"
        wait_for_console_pattern(self, verified_output);
        wait_for_console_pattern(self, verified_output);

        # Login and verify the vm is booted using secure boot
        wait_for_console_pattern(self, 'localhost login:')
        exec_command_and_wait_for_pattern(self, 'root', 'Password:')
        exec_command_and_wait_for_pattern(self, self.root_password,'[root@localhost ~]#')
        exec_command_and_wait_for_pattern(self, 'cat /sys/firmware/ipl/secure', '1')

if __name__ == '__main__':
    QemuSystemTest.main()
