#!/usr/bin/env python3
#
# Functional test that boots the Facebook SanMiguel BMC machine
#
# Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import sys

from qemu_test import Asset
from qemu_test import exec_command_and_wait_for_pattern
from aspeed import AspeedTest

# DeviceLocator's type hints require Python 3.10+; import it only when
# available. On the 3.9 baseline the module still loads and the boot
# test still runs; only the locator-based sensor checks are skipped.
if sys.version_info >= (3, 10):
    from qemu_test.locator import DeviceLocator


class SanMiguelMachine(AspeedTest):

    ASSET_SANMIGUEL_FLASH = Asset(
        'https://github.com/eblot/qemu-aspeed-boot/raw/5a972e968856bdf0493e4040ccc70455d1879511/images/'
            'sanmiguel-bmc/openbmc-20260616025450/obmc-phosphor-image-sanmiguel-20260616025450.static.mtd.xz',
        'f0b79a09cd861d79919facc16af1dbdc85a3df55b06dd62ba0318a01580d447b')

    TMP75_SENSORS = (
        ('/machine/soc::aspeed.i2c-ast2600[0]~i2c[5]~tmp75@0x48',
         '/sys/bus/i2c/devices/5-0048/hwmon/hwmon*'),
        ('/machine/soc::aspeed.i2c-ast2600[0]~i2c[9]~tmp75@0x4e',
         '/sys/bus/i2c/devices/9-004e/hwmon/hwmon*'),
        ('/machine/soc::aspeed.i2c-ast2600[0]~i2c[10]~tmp75@0x48',
         '/sys/bus/i2c/devices/10-0048/hwmon/hwmon*'),
    )
    PROMPT = 'root@sanmiguel:~#'

    def test_arm_ast2600_sanmiguel_openbmc(self):
        image_path = self.uncompress(self.ASSET_SANMIGUEL_FLASH)

        self.do_test_arm_aspeed_openbmc('sanmiguel-bmc', image=image_path,
                                        uboot='2019.04', cpu_id='0xf00',
                                        soc='AST2600 rev A3',
                                        dt_model='Facebook SanMiguel BMC')

        exec_command_and_wait_for_pattern(self, 'root', 'Password:')
        exec_command_and_wait_for_pattern(self, '0penBmc', self.PROMPT)

        # The sensor round-trip below resolves devices with DeviceLocator, which
        # requires Python 3.10+; skip just that part on older interpreters.
        if sys.version_info < (3, 10):
            return

        locator = DeviceLocator(self.vm.cmd)

        # Drive each sensor through QOM and check the kernel hwmon interface
        # reports it back; a distinct 0.5 C per-sensor step (the finest the
        # TMP75 register round-trips at any resolution) catches a mis-resolved
        # locator hitting the wrong sensor.
        for idx, (sensor, hwmon) in enumerate(self.TMP75_SENSORS):
            path = locator.resolve(sensor, 'tmp75')
            self.assertIn(b'lm75', self.read_hwmon(hwmon, 'name'))
            offset = idx * 500
            for milli_c in (55000 + offset, 30000 + offset):
                self.vm.cmd('qom-set', path=path,
                            property='temperature', value=milli_c)
                self.wait_hwmon_value(hwmon, 'temp1_input', milli_c)


if __name__ == '__main__':
    AspeedTest.main()
