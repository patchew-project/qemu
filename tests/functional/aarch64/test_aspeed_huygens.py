#!/usr/bin/env python3
#
# Functional test that boots the ASPEED Huygens machine with UFS storage
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os

from aspeed import AspeedTest

class HuygensMachine(AspeedTest):

    def test_arm_aspeed_ufs_boot(self):
        self.set_machine('huygens-bmc')
        self.require_netdev('user')

        # Huygens image is not public yet, skip if missing
        fmc_path = '/tmp/fmc-huygens.img'
        ufs_path = '/tmp/ufs-huygens.img'

        if not os.path.exists(fmc_path) or not os.path.exists(ufs_path):
            self.skipTest(f'Missing required images: {fmc_path}, {ufs_path}')

        self.vm.set_console()
        self.vm.add_args('-drive',
                         'file=' + fmc_path + ',if=mtd,format=raw',
                         '-drive',
                         'file=' + ufs_path +
                         ',if=none,format=raw,readonly=off',
                         '-nic', 'user,model=ftgmac100,net=10.0.2.0/24',
                         '-nic', 'user,model=ftgmac100,net=10.0.3.0/24',
                         '-nic', 'user,model=ftgmac100,net=10.0.4.0/24')
        self.vm.launch()

        self.wait_for_console_pattern('U-Boot 2023.10')
        self.wait_for_console_pattern(
            'Vendor: ASPEED Prod.: UFS QEMU Rev: 1.00')
        self.wait_for_console_pattern('Starting kernel ...')
        self.wait_for_console_pattern('Machine model: Huygens')
        self.wait_for_console_pattern('Starting systemd-udevd version 257.1')
        self.wait_for_console_pattern('Phosphor OpenBMC')
        self.wait_for_console_pattern('huygens login:')
        self.wait_for_console_pattern('Active BMC Target')
        self.wait_for_console_pattern('Multi-User System')
        self.vm.shutdown()


if __name__ == '__main__':
    AspeedTest.main()
