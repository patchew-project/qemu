# Functional test that boots a Linux kernel and checks the console
#
# SPDX-FileCopyrightText: 2022 Linaro Ltd.
# SPDX-FileContributor: Philippe Mathieu-Daudé <philmd@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import time

from avocado.utils import archive

from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern
from avocado_qemu import interrupt_interactive_console_until_pattern


class Aarch64SbsarefMachine(QemuSystemTest):
    """
    :avocado: tags=arch:aarch64
    :avocado: tags=machine:sbsa-ref
    """

    def test_sbsaref_tfa_v2_8(self):
        """
        :avocado: tags=cpu:cortex-a57

        Flash volumes generated using:

        - Arm GNU Toolchain version 10.3-2021.07
          https://developer.arm.com/downloads/-/gnu-a
          gcc version 10.3.1 20210621 (GNU Toolchain for the A-profile \
              Architecture 10.3-2021.07 (arm-10.29))

        - Trusted Firmware-A
          https://github.com/ARM-software/arm-trusted-firmware/blob/v2.8.0/\
              docs/plat/qemu-sbsa.rst

        - Tianocore EDK II
          https://github.com/tianocore/edk2/tree/0cb30c3f5e9b/
          https://github.com/tianocore/edk2-non-osi/tree/61662e8596dd/
          https://github.com/tianocore/edk2-platforms/tree/e2d7a3014b14/\
              Platform/Qemu/SbsaQemu

        The last URL contains the various build steps.
        """

        # Secure BootRom (TF-A code)
        fs0_xz_url = ('https://fileserver.linaro.org/s/L7BcZXJk37pKfjR/'
                      'download/SBSA_FLASH0.fd.xz')
        fs0_xz_hash = 'a865247218af268974a34f8b64af3cfddb3b59de'
        tar_xz_path = self.fetch_asset(fs0_xz_url, asset_hash=fs0_xz_hash)
        archive.extract(tar_xz_path, self.workdir)
        fs0_path = os.path.join(self.workdir, 'SBSA_FLASH0.fd')

        # Non-secure rom (UEFI and EFI variables)
        fs1_xz_url = ('https://fileserver.linaro.org/s/rNDQATTJnFCaoxb/'
                      'download/SBSA_FLASH1.fd.xz')
        fs1_xz_hash = 'b0ccf5498293d90a28c2f75a3b9906e1d65ad917'
        tar_xz_path = self.fetch_asset(fs1_xz_url, asset_hash=fs1_xz_hash)
        archive.extract(tar_xz_path, self.workdir)
        fs1_path = os.path.join(self.workdir, 'SBSA_FLASH1.fd')

        for path in [fs0_path, fs1_path]:
            with open(path, 'ab+') as fd:
                fd.truncate(256 << 20) # Expand volumes to 256MiB

        self.vm.set_console()
        self.vm.add_args('-cpu', 'cortex-a57',
                         '-drive', f'if=pflash,file={fs0_path},format=raw',
                         '-drive', f'if=pflash,file={fs1_path},format=raw')
        self.vm.launch()

        # TF-A boot sequence:
        #
        # https://github.com/ARM-software/arm-trusted-firmware/blob/v2.8.0/\
        #     docs/design/trusted-board-boot.rst#trusted-board-boot-sequence
        # https://trustedfirmware-a.readthedocs.io/en/v2.8/\
        #     design/firmware-design.html#cold-boot

        # AP Trusted ROM
        wait_for_console_pattern(self, 'Booting Trusted Firmware')
        wait_for_console_pattern(self, 'BL1: v2.7(release):v2.8-rc0')
        wait_for_console_pattern(self, 'BL1: Booting BL2')

        # Trusted Boot Firmware
        wait_for_console_pattern(self, 'BL2: v2.5(release)')
        wait_for_console_pattern(self, 'Booting BL31')

        # EL3 Runtime Software
        wait_for_console_pattern(self, 'BL31: v2.5(release)')

        # Non-trusted Firmware
        wait_for_console_pattern(self, 'UEFI firmware (version 1.0')
        interrupt_interactive_console_until_pattern(self,
                                                    'QEMU SBSA-REF Machine')
