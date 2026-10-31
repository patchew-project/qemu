#!/usr/bin/env python3
#
# Functional tests that boot Linux on a Kendryte K230 machine.
#
# The direct boot test lets QEMU load OpenSBI, Linux, the device tree, and the
# initramfs.  The firmware boot test starts the K230 SDK U-Boot and uses bootm
# to launch OpenSBI and Linux from images preloaded into RAM.
#
# Author:
#  Junze Cao
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset, LinuxKernelTest
from qemu_test import exec_command_and_wait_for_pattern
from qemu_test import skipIfMissingCommands
from qemu_test import wait_for_console_pattern


class K230Machine(LinuxKernelTest):

    # The release archive contains Image, rootfs.cpio.gz, k230-qemu.dtb,
    # u-boot, fw_jump.uImage, and boot.itb under the images/ directory.
    ASSET_IMAGES = Asset(
        ('https://github.com/processmission/qemu-machine-images/releases/'
         'download/v1.2.0/riscv64-k230-v1.2.0.tar.zst'),
        'fde65d0542127486078121c1748c7edacfe9d3ca29b5c2bcc93c67485139385c')

    def extract_images(self):
        archive = self.uncompress(self.ASSET_IMAGES, target='images.tar',
                                  format='zstd')
        self.archive_extract(archive, format='tar')

    def wait_for_linux_shell(self):
        wait_for_console_pattern(self, 'k230 login:')
        exec_command_and_wait_for_pattern(self, 'root', '# ')

    @skipIfMissingCommands('zstd')
    def test_k230_direct_boot(self):
        self.set_machine('k230')
        self.extract_images()
        kernel_path = self.scratch_file('images', 'Image')
        dtb_path = self.scratch_file('images', 'k230-qemu.dtb')
        initrd_path = self.scratch_file('images', 'rootfs.cpio.gz')

        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-initrd', initrd_path,
                         '-append', 'console=ttyS0,115200 '
                         'earlycon=uart8250,mmio32,0x91400000 cma=0',
                         '-no-reboot')
        self.vm.launch()
        self.wait_for_linux_shell()

    @skipIfMissingCommands('zstd')
    def test_k230_firmware_boot(self):
        self.set_machine('k230')
        self.extract_images()
        uboot_path = self.scratch_file('images', 'u-boot')
        boot_itb_path = self.scratch_file('images', 'boot.itb')

        self.vm.set_console()
        self.vm.add_args(
            '-bios', uboot_path,
            '-device',
            f'loader,file={boot_itb_path},addr=0xc100000,force-raw=on',
            '-no-reboot')
        self.vm.launch()

        wait_for_console_pattern(self, 'Starting kernel ...')
        self.wait_for_linux_shell()


if __name__ == '__main__':
    LinuxKernelTest.main()
