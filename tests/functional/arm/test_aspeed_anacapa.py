#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import re
import time

from qemu_test import Asset
from aspeed import AspeedTest
from qemu_test import exec_command_and_wait_for_pattern


class AnacapaMachine(AspeedTest):

    ASSET_ANACAPA_FLASH = Asset(
        'https://github.com/legoater/qemu-aspeed-boot/raw/refs/heads/master/images/anacapa-bmc/openbmc-20260616025349/obmc-phosphor-image-anacapa-20260616025349.static.mtd.xz',
        'de3841fb6ed3085aec6424358ee6efc4b8ee85688361e5aa1987fd1acb7d3fb4')

    ADC128D818_QOM_PATH = "/machine/labels/i2c8:0:1f"
    ADC128D818_MUX_CHANNEL = "/sys/bus/i2c/devices/8-0072/channel-0"
    PROMPT = "root@anacapa:~#"

    def test_arm_ast2600_anacapa_openbmc(self):
        image_path = self.uncompress(self.ASSET_ANACAPA_FLASH)

        self.do_test_arm_aspeed_openbmc('anacapa-bmc', image=image_path,
                                        uboot='2019.04', cpu_id='0xf00',
                                        soc='AST2600 rev A3')

        exec_command_and_wait_for_pattern(self, "root", "Password:")
        exec_command_and_wait_for_pattern(self, "0penBmc", "#")

        self.adc_hwmon = self.resolve_adc128d818_hwmon()
        self.assertIn(b"adc128d818", self.read_adc128d818("name"))

        adc = self.ADC128D818_QOM_PATH
        for ch0_mv, ch1_mv in ((108, 2000), (1280, 500)):
            self.vm.cmd("qom-set", path=adc, property="ain0", value=ch0_mv)
            self.vm.cmd("qom-set", path=adc, property="ain1", value=ch1_mv)
            self.wait_adc128d818_value("in0_input", ch0_mv)
            self.wait_adc128d818_value("in1_input", ch1_mv)

        self.assertIn(b"2551", self.read_adc128d818("in0_max"))
        self.assertRegex(self.read_adc128d818("in0_min"), rb"(?m)^0\r*$")

    def resolve_adc128d818_hwmon(self):
        out = self.read_adc128d818_console(
            f"basename $(readlink {self.ADC128D818_MUX_CHANNEL})"
        )
        match = re.search(rb"i2c-(\d+)", out)
        self.assertIsNotNone(match, "could not resolve ADC128D818 i2c bus")
        bus = int(match.group(1))
        return f"/sys/bus/i2c/devices/{bus}-001f/hwmon/hwmon*"

    def read_adc128d818_console(self, command):
        return exec_command_and_wait_for_pattern(self, command, self.PROMPT)

    def read_adc128d818(self, attr):
        return self.read_adc128d818_console(f"cat {self.adc_hwmon}/{attr}")

    def wait_adc128d818_value(self, attr, expected):
        needle = str(expected).encode()
        if needle in self.read_adc128d818(attr):
            return
        time.sleep(2)
        if needle not in self.read_adc128d818(attr):
            self.fail(f"{attr} did not reach {expected}")


if __name__ == '__main__':
    AspeedTest.main()
