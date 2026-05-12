#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from aspeed import AspeedTest

from qemu_test import wait_for_console_pattern, exec_command
from qemu_test import exec_command_and_wait_for_pattern

class YosemiteV4Machine(AspeedTest):

    ASSET_YOSEMITE_V4_FLASH = Asset(
        'https://github.com/legoater/qemu-aspeed-boot/raw/refs/heads/master/images/yosemite4-bmc/openbmc-20260505132843/obmc-phosphor-image-yosemite4-20260505132843.static.mtd.xz',
        'dff6946363b41f952b15cfc3156482b89fcfc1b0ecfc3ec8b3ed496a5f001ef9')

    def do_test_arm_aspeed_openbmc_no_network(self, machine, image, uboot,
                                   cpu_id, soc):

        self.set_machine(machine)
        self.vm.set_console()
        self.vm.add_args('-drive', f'file={image},if=mtd,format=raw',
                         '-snapshot')
        self.vm.launch()

        self.wait_for_console_pattern(f'U-Boot {uboot}')
        self.wait_for_console_pattern('## Loading kernel from FIT Image')
        self.wait_for_console_pattern('Starting kernel ...')
        self.wait_for_console_pattern(f'Booting Linux on physical CPU {cpu_id}')
        self.wait_for_console_pattern(f'ASPEED {soc}')
        self.wait_for_console_pattern('/init as init process')
        # yosemite v4 does not emit the hostname log which is
        # different from the other machines.
        self.wait_for_console_pattern('yosemite4 login:')

        # perform login
        exec_command_and_wait_for_pattern(self,
                                          "root", "Password:");

        exec_command_and_wait_for_pattern(self, "0penBmc", "#");

        # MAX31790 test
        exec_command_and_wait_for_pattern(self,
            "cat /sys/class/hwmon/hwmon2/name", "max31790");
        exec_command_and_wait_for_pattern(self,
            "cat /sys/class/hwmon/hwmon2/fan1_input", "4530");
        exec_command_and_wait_for_pattern(self,
            "cat /sys/class/hwmon/hwmon2/fan1_enable", "1");
        exec_command_and_wait_for_pattern(self,
            "cat /sys/class/hwmon/hwmon2/fan1_fault", "0");

    def test_arm_ast2600_yosemitev4_openbmc(self):
        image_path = self.uncompress(self.ASSET_YOSEMITE_V4_FLASH)

        self.do_test_arm_aspeed_openbmc_no_network('fby4-bmc', image=image_path,
                                        uboot='2019.04', cpu_id='0xf00',
                                        soc='AST2600 rev A3')

if __name__ == '__main__':
    AspeedTest.main()
