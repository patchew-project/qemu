# Functional test that boots a Linux kernel and checks the console
#
# SPDX-FileCopyrightText: 2023 Linaro Ltd.
# SPDX-FileContributor: Philippe Mathieu-Daudé <philmd@linaro.org>
# SPDX-FileContributor: Marcin Juszkiewicz <marcin.juszkiewicz@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import shutil

from avocado.utils import archive

from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern
from avocado_qemu import interrupt_interactive_console_until_pattern


class Aarch64SbsarefMachine(QemuSystemTest):
    """
    :avocado: tags=arch:aarch64
    :avocado: tags=machine:sbsa-ref
    """

    def fetch_firmware(self):
        """
        Flash volumes generated using:

        - Fedora GNU Toolchain version 12.2.1 20220819 (Red Hat Cross 12.2.1-2)

        - Trusted Firmware-A
          https://github.com/ARM-software/arm-trusted-firmware/tree/6264643a

        - Tianocore EDK II
          https://github.com/tianocore/edk2/tree/f6ce1a5c
          https://github.com/tianocore/edk2-non-osi/tree/74d4da60
          https://github.com/tianocore/edk2-platforms/tree/0540e1a2
        """

        # Secure BootRom (TF-A code)
        fs0_xz_url = ('https://fileserver.linaro.org/s/sZay4ZCCfHSXPKj/'
                      'download/SBSA_FLASH0.fd.xz')
        fs0_xz_hash = 'e74778cbb8e1aa0b77f8883565b9a18db638f6bb'
        tar_xz_path = self.fetch_asset(fs0_xz_url, asset_hash=fs0_xz_hash)
        archive.extract(tar_xz_path, self.workdir)
        fs0_path = os.path.join(self.workdir, 'SBSA_FLASH0.fd')

        # Non-secure rom (UEFI and EFI variables)
        fs1_xz_url = ('https://fileserver.linaro.org/s/osHNaypByLa9xDK/'
                      'download/SBSA_FLASH1.fd.xz')
        fs1_xz_hash = '7d9f1a6b8964b8b99144f7e905a4083f31e31ad3'
        tar_xz_path = self.fetch_asset(fs1_xz_url, asset_hash=fs1_xz_hash)
        archive.extract(tar_xz_path, self.workdir)
        fs1_path = os.path.join(self.workdir, 'SBSA_FLASH1.fd')

        for path in [fs0_path, fs1_path]:
            with open(path, 'ab+') as fd:
                fd.truncate(256 << 20)  # Expand volumes to 256MiB

        self.vm.set_console()
        self.vm.add_args('-drive', f'if=pflash,file={fs0_path},format=raw',
                         '-drive', f'if=pflash,file={fs1_path},format=raw')

    def test_sbsaref_tfa_v2_8(self):
        """
        :avocado: tags=cpu:cortex-a57
        """

        self.fetch_firmware()
        self.vm.launch()

        # TF-A boot sequence:
        #
        # https://github.com/ARM-software/arm-trusted-firmware/blob/v2.8.0/\
        #     docs/design/trusted-board-boot.rst#trusted-board-boot-sequence
        # https://trustedfirmware-a.readthedocs.io/en/v2.8/\
        #     design/firmware-design.html#cold-boot

        # AP Trusted ROM
        wait_for_console_pattern(self, 'Booting Trusted Firmware')
        wait_for_console_pattern(self, 'BL1: v2.8(release):v2.8')
        wait_for_console_pattern(self, 'BL1: Booting BL2')

        # Trusted Boot Firmware
        wait_for_console_pattern(self, 'BL2: v2.8(release)')
        wait_for_console_pattern(self, 'Booting BL31')

        # EL3 Runtime Software
        wait_for_console_pattern(self, 'BL31: v2.8(release)')

        # Non-trusted Firmware
        wait_for_console_pattern(self, 'UEFI firmware (version 1.0')
        interrupt_interactive_console_until_pattern(self,
                                                    'QEMU SBSA-REF Machine')

    def boot_linux(self, cpu='cortex-a57'):
        """
        :avocado: tags=cpu:cortex-a57
        """
        self.fetch_firmware()

        os.makedirs(f'{self.workdir}/vfat/efi/boot')

        # UEFI shell binary
        shell_url = ('https://fileserver.linaro.org/s/SGoyRrEzkmW8C8Y/'
                     'download/bootaa64.efi')
        shell_sha1 = '5a8791eb130406d1a659e538b1a194a604a29a78'
        shell_path = self.fetch_asset(shell_url, shell_sha1)
        shutil.copyfile(shell_path,
                        f'{self.workdir}/vfat/efi/boot/bootaa64.efi')

        # Debian 'bookworm' d-i kernel from 8th Feb 2023
        linux_url = ('https://fileserver.linaro.org/s/L8JMwEZQK8SDR39/'
                     'download/linux')
        linux_sha1 = '39a75284783ab63626642228fbac1863492d30b5'
        linux_path = self.fetch_asset(linux_url, linux_sha1)
        shutil.copyfile(linux_path, f'{self.workdir}/vfat/linux')

        # Debian 'bookworm' d-i initrd.gz from 8th Feb 2023
        initrd_url = ('https://fileserver.linaro.org/s/NmYTxezZNKGF5P4/'
                      'download/initrd.gz')
        initrd_sha1 = '1404d0129cbd0bff7aaa589ddbea3cdb7c0d4c1d'
        initrd_path = self.fetch_asset(initrd_url, initrd_sha1)
        shutil.copyfile(initrd_path, f'{self.workdir}/vfat/initrd.gz')

        with open(f'{self.workdir}/vfat/startup.nsh', 'w') as script:
            script.write('fs0:\\linux initrd=\\initrd.gz init=/bin/sh')

        self.vm.add_args('-cpu', cpu,
                         '-drive',
                         f'file=fat:rw:{self.workdir}/vfat/,format=raw')
        self.vm.launch()

        # Exit UEFI
        wait_for_console_pattern(self, 'EFI stub: Exiting boot services...')

        # init=/bin/sh started
        wait_for_console_pattern(self, 'BusyBox v1.35.0 (Debian')

    def test_sbsaref_linux_a57(self):
        """
        :avocado: tags=cpu:cortex-a57
        """
        self.boot_linux('cortex-a57')

    def test_sbsaref_linux_max(self):
        """
        :avocado: tags=cpu:max
        """
        self.boot_linux('max')
