#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

import re
import time

from qemu_test import Asset
from qemu_test import exec_command
from qemu_test import exec_command_and_wait_for_pattern
from qemu_test import wait_for_console_pattern
from aspeed import FacebookAspeedTest


class AnacapaMachine(FacebookAspeedTest):

    ASSET_ANACAPA_FLASH = Asset(
        "https://github.com/legoater/qemu-aspeed-boot/raw/3fa3212827b04be4034d43b5adeef57c27d6ab18/images/anacapa-bmc/openbmc-20260512025228/obmc-phosphor-image-anacapa-20260512025228.static.mtd.xz",
        "2232e241abcfb6d4f6b82cb6c378ce5ce05e364aac6d118785c2b6cc33fe43f3",
    )

    # ADC128D818 on i2c8 mux (PCA9546 @0x72) channel 0, address 0x1f.
    ADC128D818_HWMON = "/sys/bus/i2c/devices/*-001f/hwmon/hwmon*"

    def test_arm_ast2600_anacapa_openbmc(self):
        image_path = self.uncompress(self.ASSET_ANACAPA_FLASH)

        self.do_test_arm_aspeed_openbmc(
            "anacapa-bmc",
            image=image_path,
            uboot="2019.04",
            cpu_id="0xf00",
            soc="AST2600 rev A3",
        )

        # perform login
        exec_command_and_wait_for_pattern(self, "root", "Password:")
        exec_command_and_wait_for_pattern(self, "0penBmc", "#")

        # ADC128D818 test
        #
        # The current Anacapa OpenBMC device tree is the AST2600 EVB one, and
        # therefore does not declare the i2c8 mux nor the ADC128D818 behind it,
        # so bind the emulated PCA9546 (@0x72) and then the ADC128D818 (@0x1f,
        # mux channel 0) from userspace before probing.
        # This should not longer be useful once the Anacapa DT is updated.
        exec_command_and_wait_for_pattern(
            self,
            "echo pca9546 0x72 > /sys/bus/i2c/devices/i2c-8/new_device",
            "root@anacapa",
        )
        exec_command_and_wait_for_pattern(
            self,
            "bus=$(basename $(readlink /sys/bus/i2c/devices/8-0072/channel-0)); "
            "echo adc128d818 0x1f > /sys/bus/i2c/devices/$bus/new_device",
            "root@anacapa",
        )
        exec_command_and_wait_for_pattern(
            self, f"cat {self.ADC128D818_HWMON}/name", "adc128d818"
        )

        adc = self.find_adc128d818_qom_path()
        for ch0_mv, ch1_mv in ((108, 2000), (1280, 500)):
            self.vm.cmd("qom-set", path=adc, property="ain0", value=ch0_mv)
            self.vm.cmd("qom-set", path=adc, property="ain1", value=ch1_mv)
            self.wait_adc128d818_value("in0_input", ch0_mv)
            self.wait_adc128d818_value("in1_input", ch1_mv)

        self.assertEqual(self.read_adc128d818_value("in0_min"), 0)
        self.assertEqual(self.read_adc128d818_value("in0_max"), 2551)

    def find_adc128d818_qom_path(self):
        unattached = "/machine/unattached"
        devices = [
            child["name"]
            for child in self.vm.cmd("qom-list", path=unattached)
            if "adc128d818" in child["type"]
        ]
        devices.sort(key=lambda name: int(name[len("device[") : -1]))
        return f"{unattached}/{devices[0]}"

    def read_adc128d818_value(self, attr):
        # split the marker via $m so it only appears in the output, not echo
        path = f"{self.ADC128D818_HWMON}/{attr}"
        exec_command(self, f'm=END; cat {path}; echo "ADC$m"')
        out = wait_for_console_pattern(self, "ADCEND")
        match = re.search(rb"(-?\d+)\s+ADCEND", out)
        self.assertIsNotNone(match, f"could not read {attr}")
        return int(match.group(1))

    def wait_adc128d818_value(self, attr, expected, timeout=20):
        deadline = time.monotonic() + timeout
        value = None
        while time.monotonic() < deadline:
            value = self.read_adc128d818_value(attr)
            if value == expected:
                return
            time.sleep(2)
        self.fail(f"{attr} did not reach {expected} (last read {value})")


if __name__ == "__main__":
    FacebookAspeedTest.main()
