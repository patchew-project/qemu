#!/usr/bin/env python3
#
# Functional test that boots HSS, U-Boot, and Linux on an Icicle Kit.
#
# Copyright (c) 2026 Process Mission
#
# Author:
#   Bin Meng <bin.meng@processmission.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset, LinuxKernelTest, skipIfMissingCommands


class MicrochipIcicleKitMachine(LinuxKernelTest):

    ASSET_IMAGES = Asset(
        ('https://github.com/processmission/qemu-machine-images/releases/'
         'download/v1.0.0/'
         'riscv64-microchip-icicle-kit-v1.0.0.tar.zst'),
        'fe7eb3ee737fd15d6d5a8480822b070923ae4d611b19ed03983e11f35b6aac04')

    @skipIfMissingCommands('zstd')
    def test_microchip_icicle_kit_boot(self):
        self.set_machine('microchip-icicle-kit')

        archive_path = self.uncompress(
            self.ASSET_IMAGES,
            target='microchip_icicle_kit_images.tar',
            format='zstd')
        self.archive_extract(archive_path, format='tar')
        firmware_path = self.scratch_file('images', 'hss.bin')
        sdcard_path = self.scratch_file('images', 'sdcard.img')

        self.vm.set_console(console_index=1)
        self.vm.add_args(
            '-smp', '5',
            '-m', '2G',
            '-display', 'none',
            '-bios', firmware_path,
            '-snapshot',
            '-drive', f'file={sdcard_path},format=raw,if=sd,index=0',
            '-no-reboot')
        self.vm.launch()

        self.wait_for_console_pattern('U-Boot ')
        self.wait_for_console_pattern('Starting kernel ...')
        self.wait_for_console_pattern(
            'Machine model: Microchip PolarFire-SoC Icicle Kit')
        self.wait_for_console_pattern('using ADMA 64-bit')
        self.wait_for_console_pattern('VFS: Mounted root')
        self.wait_for_console_pattern('mpfs_icicle login:')


if __name__ == '__main__':
    LinuxKernelTest.main()
