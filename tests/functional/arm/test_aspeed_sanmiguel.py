#!/usr/bin/env python3
#
# Functional test that boots the SanMiguel BMC machine
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from aspeed import FacebookAspeedTest


class SanMiguelMachine(FacebookAspeedTest):

    ASSET_SANMIGUEL_FLASH = Asset(
        "https://github.com/eblot/qemu-aspeed-boot/raw/refs/heads/sanmiguel-bmc/images/sanmiguel-bmc/"
            "openbmc-20260616025450/obmc-phosphor-image-sanmiguel-20260616025450.static.mtd.xz",
        "f0b79a09cd861d79919facc16af1dbdc85a3df55b06dd62ba0318a01580d447b",
    )

    def test_arm_ast2600_sanmiguel_openbmc(self):
        image_path = self.uncompress(self.ASSET_SANMIGUEL_FLASH)

        self.do_test_arm_aspeed_openbmc(
            "sanmiguel-bmc",
            image=image_path,
            uboot="2019.04",
            cpu_id="0xf00",
            soc="AST2600 rev A3",
            dt_model="Facebook SanMiguel BMC",
        )


if __name__ == "__main__":
    FacebookAspeedTest.main()
