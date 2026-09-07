#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from aspeed import AspeedTest


class MinervaMachine(AspeedTest):

    # TODO: the boot image currently lives on a personal fork; move it under
    # legoater/qemu-aspeed-boot before upstreaming.
    ASSET_MINERVA_FLASH = Asset(
        'https://github.com/eblot/qemu-aspeed-boot/raw/refs/heads/minerva-bmc/images/minerva-bmc/openbmc-20260720025047/'
            'obmc-phosphor-image-minerva-20260720025047.static.mtd.xz',
        'bd713df39d3c09846299a2377315465187a4dcd33c29d3b3d683ef13b59f36bb')

    def test_arm_ast2600_minerva_openbmc(self):
        image_path = self.uncompress(self.ASSET_MINERVA_FLASH)

        self.do_test_arm_aspeed_openbmc('minerva-bmc', image=image_path,
                                        uboot='2019.04', cpu_id='0xf00',
                                        soc='AST2600 rev A3')

if __name__ == '__main__':
    AspeedTest.main()
