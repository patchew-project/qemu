# Functional test for ROCK 5B SPL, U-Boot and direct Linux boot
#
# Copyright (C) 2026 Process Mission
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset, QemuSystemTest, skipIfMissingCommands
from qemu_test import exec_command, exec_command_and_wait_for_pattern
from qemu_test import wait_for_console_pattern


class Rock5BMachine(QemuSystemTest):

    ASSET_IMAGE = Asset(
        ('https://github.com/processmission/qemu-machine-images/releases/'
         'download/v1.2.0/aarch64-rock-5b-v1.2.0.tar.zst'),
        'bbec789f25a5e87adb1aeabf58294551d2a26bbf076e0041935f307a9492ce6f')

    UBOOT_PATTERNS = ('U-Boot SPL 2025.01',
                      'Trying to boot from SPI',
                      'U-Boot 2025.01',
                      'SF: Detected xt25f128',
                      'SF: 12582912 bytes @ 0x400000 Read: OK',
                      'Starting kernel')
    LINUX_PATTERNS = ('Booting Linux on physical CPU',)
    LINUX_APPEND = ('console=ttyS2,1500000n8 '
                    'earlycon=uart8250,mmio32,0xfeb50000 '
                    'rdinit=/init')

    def _extract_images(self) -> None:
        self.require_accelerator('tcg')
        self.set_machine('rock-5b')
        archive = self.uncompress(self.ASSET_IMAGE, target='images.tar',
                                  format='zstd')
        self.archive_extract(archive, format='tar')

    def _boot_and_login(self, patterns: tuple[str, ...]) -> None:
        for pattern in patterns + ('rock5b login:',):
            wait_for_console_pattern(self, pattern, 'Kernel panic')
        exec_command_and_wait_for_pattern(self, 'root', '# ')

    def _check_guest(self, command: str) -> None:
        # Split the marker so an echoed command cannot satisfy the check.
        exec_command_and_wait_for_pattern(
            self,
            command + " && printf '\\nROCK5B_%s\\n' OK"
                      " || printf '\\nROCK5B_%s\\n' FAIL",
            'ROCK5B_OK', 'ROCK5B_FAIL')
        wait_for_console_pattern(self, '# ')

    def _check_board(self) -> None:
        self._check_guest('test "$(uname -m)" = aarch64')
        self._check_guest('test "$(grep -c ^processor /proc/cpuinfo)" -eq 8')
        self._check_guest('test -c /dev/ttyS2 && '
                          'test -d /sys/devices/system/cpu/cpu7')
        self._check_guest(
            'test "$(tr -d \'\\000\' < /proc/device-tree/model)" '
            '= \'Radxa ROCK 5B\'')
        self._check_guest(
            "tr '\\000' '\\n' < /proc/device-tree/compatible | "
            "grep -qx 'radxa,rock-5b'")
        self._check_guest(
            "printf 'rock5b-boot\\n' > /root/boot-check && "
            'test "$(cat /root/boot-check)" = rock5b-boot')

    def _reboot_and_poweroff(self, patterns: tuple[str, ...]) -> None:
        exec_command(self, 'reboot -f')
        self._boot_and_login(patterns)
        self._check_guest('test "$(grep -c ^processor /proc/cpuinfo)" -eq 8')
        self._check_guest('test ! -e /root/boot-check')

        exec_command_and_wait_for_pattern(self, 'poweroff -f',
                                          'reboot: Power down')
        self.vm.wait()
        self.assertEqual(self.vm.exitcode(), 0)

    @skipIfMissingCommands('zstd')
    def test_flash_boot(self) -> None:
        self._extract_images()
        firmware = self.scratch_file('images', 'u-boot-spl.bin')
        flash = self.scratch_file('images', 'spi-nor.img')

        self.vm.set_console()
        self.vm.add_args('-accel', 'tcg', '-m', '2G', '-smp', '8',
                         '-bios', firmware,
                         '-drive', f'if=mtd,index=0,file={flash},'
                                   'format=raw,snapshot=on')
        self.vm.launch()
        self._boot_and_login(self.UBOOT_PATTERNS)
        self._check_board()
        self._reboot_and_poweroff(self.UBOOT_PATTERNS)

    @skipIfMissingCommands('zstd')
    def test_direct_boot(self) -> None:
        self._extract_images()

        self.vm.set_console()
        # 5 GiB of RAM continues above the peripheral hole at 0x100000000.
        self.vm.add_args('-accel', 'tcg', '-m', '5G', '-smp', '8',
                         '-kernel', self.scratch_file('images', 'Image'),
                         '-initrd',
                         self.scratch_file('images', 'rootfs.cpio.gz'),
                         '-dtb',
                         self.scratch_file('images', 'rk3588-rock-5b.dtb'),
                         '-append', self.LINUX_APPEND)
        self.vm.launch()
        self._boot_and_login(self.LINUX_PATTERNS)
        self._check_board()
        self._check_guest('grep -q rdinit=/init /proc/cmdline')
        self._check_guest('test -d /proc/device-tree/memory@100000000')
        # Low RAM ends at 0xf0000000, which is 3932160 KiB.
        self._check_guest(
            'test "$(awk \'/^MemTotal/ {print $2}\' /proc/meminfo)" '
            '-gt 3932160')
        self._reboot_and_poweroff(self.LINUX_PATTERNS)


if __name__ == '__main__':
    QemuSystemTest.main()
