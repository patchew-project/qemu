#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
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

    # io_expander0: pca9555@20 on i2c2, a 16-bit expander directly on the bus.
    IOEXP0_LOCATOR = "/machine/soc::aspeed.i2c-ast2600[0]~i2c[2]~pca9555@0x20"
    IOEXP0_GPIODETECT = r"\[2-0020\]"
    # io_expander5: pca9554@27 behind pca9548@70 channel 6 on i2c1, an 8-bit
    # expander reached through the mux; gpiodetect confirms its 8 lines.
    IOEXP5_LOCATOR = ("/machine/soc::aspeed.i2c-ast2600[0]"
                      "~i2c[1]~pca9548@0x70~i2c[6]~pca9554@0x27")
    IOEXP5_GPIODETECT = r"-0027] \(8 lines\)"

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

        locator = DeviceLocator(self.vm.cmd)
        tmp75 = locator.resolve(self.TMP75_LOCATOR, "tmp75")
        ioexp0 = locator.resolve(self.IOEXP0_LOCATOR, "pca9555")
        ioexp5 = locator.resolve(self.IOEXP5_LOCATOR, "pca9554")

        # temp1_input reports the temperature in millidegrees Celsius. Drive it
        # through QOM (units of 0.001 C) and check the kernel hwmon interface
        # reports the injected value back.
        for milli_c in (55000, 30000):
            self.vm.cmd("qom-set", path=tmp75,
                        property="temperature", value=milli_c)
            self.wait_hwmon_value(self.TMP75_HWMON, "temp1_input", milli_c)

        # pca9555@20 spans two 8-bit ports (pins 0-7 and 8-15).
        self.check_ioexp(ioexp0, self.IOEXP0_GPIODETECT, (0, 7, 8, 15))
        # pca9554@27 is a single 8-bit port.
        self.check_ioexp(ioexp5, self.IOEXP5_GPIODETECT, (0, 3, 7))

    def check_ioexp(self, qom, gpiodetect, pins):
        # The guest drives these lines as inputs: set_pin injects an external
        # level, read back over QOM and gpioget. Outputs are covered by qtests.
        chip = self.resolve_gpiochip(gpiodetect)
        for pin in pins:
            self.set_pin(qom, pin, "low")
            self.assert_pin(qom, pin, "low")
            self.assert_gpio_line(chip, pin, 0)
            self.set_pin(qom, pin, "high")
            self.assert_pin(qom, pin, "high")
            self.assert_gpio_line(chip, pin, 1)

        # Drive two pins to opposite levels at once to confirm they are
        # reported back independently.
        lo, hi = pins[0], pins[-1]
        self.set_pin(qom, lo, "low")
        self.set_pin(qom, hi, "high")
        self.assert_pin(qom, lo, "low")
        self.assert_pin(qom, hi, "high")
        self.assert_gpio_line(chip, lo, 0)
        self.assert_gpio_line(chip, hi, 1)

    def read_catalina_console(self, command):
        return exec_command_and_wait_for_pattern(self, command, self.PROMPT)

    def resolve_gpiochip(self, gpiodetect):
        # awk already prints the "gpiochipN" name; just pick it out of the
        # console output (the command echo and prompt hold no such token).
        out = self.read_catalina_console(
            f"gpiodetect | awk '/{gpiodetect}/{{print $1}}'")
        for token in out.split():
            if token.startswith(b"gpiochip"):
                return token.decode()
        self.fail(f"could not resolve gpiochip {gpiodetect}")

    def set_pin(self, qom, pin, level):
        self.vm.cmd("qom-set", path=qom, property=f"pin{pin}", value=level)

    def get_pin(self, qom, pin):
        return self.vm.cmd("qom-get", path=qom, property=f"pin{pin}")

    def assert_pin(self, qom, pin, expected):
        level = self.get_pin(qom, pin)
        self.assertEqual(level, expected,
                         f"pin{pin} QOM read {level!r}, expected {expected!r}")

    def assert_gpio_line(self, chip, pin, expected):
        out = self.read_catalina_console(
            f"echo LINE{pin}=$(gpioget {chip} {pin})")
        self.assertIn(f"LINE{pin}={expected}".encode(), out)


if __name__ == '__main__':
    AspeedTest.main()
