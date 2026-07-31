#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

import sys

from qemu_test import Asset
from qemu_test import exec_command_and_wait_for_pattern
from aspeed import AspeedTest

# DeviceLocator's type hints require Python 3.10+; import it only when
# available. On the 3.9 baseline the module still loads and the boot
# test still runs; only the locator-based device checks are skipped.
if sys.version_info >= (3, 10):
    from qemu_test.locator import DeviceLocator


class CatalinaMachine(AspeedTest):

    ASSET_CATALINA_FLASH = Asset(
        'https://github.com/legoater/qemu-aspeed-boot/raw/a866feb5ef81245b4827a214584bf6bcc72939f6/images/'
            'catalina-bmc/obmc-phosphor-image-catalina-20250619123021.static.mtd.xz',
        '287402e1ba021991e06be1d098f509444a02a3d81a73a932f66528b159e864f9')

    TMP75_LOCATOR = "/machine/soc::aspeed.i2c-ast2600[0]~i2c[9]~tmp75@0x4b"
    TMP75_HWMON = "/sys/bus/i2c/devices/9-004b/hwmon/hwmon*"
    PROMPT = "root@catalina:~#"

    def test_arm_ast2600_catalina_openbmc(self):
        image_path = self.uncompress(self.ASSET_CATALINA_FLASH)

        self.do_test_arm_aspeed_openbmc('catalina-bmc', image=image_path,
                                        uboot='2019.04', cpu_id='0xf00',
                                        soc='AST2600 rev A3')

        exec_command_and_wait_for_pattern(self, 'root', 'Password:')
        exec_command_and_wait_for_pattern(self, '0penBmc', self.PROMPT)

        self.assertIn(b"tmp75", self.read_hwmon(self.TMP75_HWMON, "name"))

        # The QOM round-trips below resolve devices with DeviceLocator, which
        # require Python 3.10+; skip just those on older interpreters.
        if sys.version_info < (3, 10):
            return

        tmp75 = DeviceLocator(self.vm.cmd).resolve(self.TMP75_LOCATOR, "tmp75")

        # temp1_input reports the temperature in millidegrees Celsius. Drive it
        # through QOM (units of 0.001 C) and check the kernel hwmon interface
        # reports the injected value back.
        for milli_c in (55000, 30000):
            self.vm.cmd("qom-set", path=tmp75,
                        property="temperature", value=milli_c)
            self.wait_hwmon_value(self.TMP75_HWMON, "temp1_input", milli_c)


if __name__ == '__main__':
    AspeedTest.main()
