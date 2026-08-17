#!/usr/bin/env python3
#
# RISC-V virt RPMI Linux guest integration tests
#
# Copyright (c) 2026 Qualcomm Technologies, Inc.
#
# Author:
#  Subrahmanya Lingappa <subrahmanya.lingappa@oss.qualcomm.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
from pathlib import Path
import subprocess
import time

from qemu_test import QemuSystemTest
from qemu_test import exec_command
from qemu_test import exec_command_and_wait_for_pattern
from qemu_test import get_qemu_img
from qemu_test import wait_for_console_pattern


class RiscvVirtRpmiGuestTest(QemuSystemTest):
    """Boot Linux and exercise RPMI-backed services through guest APIs.

    This test intentionally keeps guest artifacts external. Set:

      QEMU_TEST_RPMI_OPENSBI=/path/to/fw_jump.bin
      QEMU_TEST_RPMI_KERNEL=/path/to/Image
      QEMU_TEST_RPMI_ROOTFS=/path/to/rootfs.ext4

    Exact RPMI packet command-ID coverage remains in riscv-rpmi qtests. This
    functional test verifies Linux/OpenSBI integration paths visible from a
    real guest without adding probe hooks to production RPMI sources.
    """

    timeout = 360
    PROMPT = 'root@tuxtest:~#'

    def _require_path(self, env_name):
        value = os.getenv(env_name)
        if not value:
            self.skipTest(f'{env_name} is required')
        path = Path(value)
        if not path.exists():
            self.skipTest(f'{env_name} does not exist: {path}')
        return path

    def setUp(self):
        super().setUp()
        self.opensbi = self._require_path('QEMU_TEST_RPMI_OPENSBI')
        self.kernel = self._require_path('QEMU_TEST_RPMI_KERNEL')
        self.rootfs = self._require_path('QEMU_TEST_RPMI_ROOTFS')
        self.qemu_img = get_qemu_img(self)

    def _create_rootfs_overlay(self):
        overlay = Path(self.scratch_file('rootfs.qcow2'))
        subprocess.run([
            self.qemu_img, 'create', '-q', '-f', 'qcow2', '-F', 'raw',
            '-b', str(self.rootfs), str(overlay),
        ], check=True)
        return overlay

    def _boot_guest(self, smp=4):
        self.set_machine('virt')
        self.machine = 'virt,rpmi=on,aia=aplic-imsic'
        rootfs_overlay = self._create_rootfs_overlay()

        self.vm.set_console()
        self.vm.add_args(
            '-cpu', 'rv64',
            '-smp', str(smp),
            '-m', '1G',
            '-bios', str(self.opensbi),
            '-kernel', str(self.kernel),
            '-append', 'printk.time=0 root=/dev/vda rw console=ttyS0 '
                       'earlycon=sbi loglevel=8 no_console_suspend',
            '-blockdev', 'driver=qcow2,file.driver=file,'
                         f'file.filename={rootfs_overlay},node-name=hd0',
            '-device', 'virtio-blk-device,drive=hd0',
        )
        self.vm.launch()

    def _expect_rpmi_firmware_and_linux(self):
        patterns = [
            'Platform HSM Device         : rpmi-hsm',
            'Platform Reboot Device      : rpmi-system-reset',
            'Platform Shutdown Device    : rpmi-system-reset',
            'Platform Suspend Device     : rpmi-system-suspend',
            'Standard SBI Extensions',
            'susp',
            'tuxtest login:',
        ]
        for pattern in patterns:
            wait_for_console_pattern(self, pattern)
        exec_command_and_wait_for_pattern(self, 'root', self.PROMPT)

    def _run_marker_command(self, command, marker, failure_marker=None):
        out = exec_command_and_wait_for_pattern(self, command, marker,
                                                failure_message=failure_marker)
        self.assertIn(marker.encode(), out)
        return out

    def _validate_hsm_boot_start(self, online_cpus):
        self._run_marker_command(
            'echo RPMI_LINUX_HSM_START_BEGIN; '
            'cat /sys/devices/system/cpu/present; '
            'online=$(cat /sys/devices/system/cpu/online); '
            'echo "$online"; '
            f'if [ "$online" = "{online_cpus}" ]; '
            'then echo RPMI_LINUX_HSM_START_PASS; '
            'else echo RPMI_LINUX_HSM_START_FAIL; fi',
            'RPMI_LINUX_HSM_START_PASS', 'RPMI_LINUX_HSM_START_FAIL')

    def _validate_hsm_stop(self):
        self._run_marker_command(
            'echo RPMI_LINUX_HSM_STOP_BEGIN; '
            'if [ -w /sys/devices/system/cpu/cpu1/online ]; then '
            'echo 0 > /sys/devices/system/cpu/cpu1/online && '
            '[ "$(cat /sys/devices/system/cpu/online)" = 0 ] && '
            'echo RPMI_LINUX_HSM_STOP_PASS || echo RPMI_LINUX_HSM_STOP_FAIL; '
            'else echo RPMI_LINUX_HSM_STOP_FAIL; fi',
            'RPMI_LINUX_HSM_STOP_PASS', 'RPMI_LINUX_HSM_STOP_FAIL')

    def _validate_system_suspend(self):
        exec_command(
            self,
            'echo RPMI_LINUX_SUSPEND_BEGIN; '
            'if grep -qw mem /sys/power/state; then '
            'echo mem > /sys/power/state; '
            'else echo RPMI_LINUX_SUSPEND_FAIL; fi')
        wait_for_console_pattern(self, 'RPMI_LINUX_SUSPEND_BEGIN')
        wait_for_console_pattern(self, 'PM: suspend entry')
        self.vm.event_wait('SUSPEND', 10)
        self.vm.qmp('system_wakeup')
        self.vm.event_wait('WAKEUP', 10)
        wait_for_console_pattern(self, 'PM: suspend exit')
        wait_for_console_pattern(self, self.PROMPT)
        self._run_marker_command(
            'marker=RPMI_LINUX_WAKEUP; echo ${marker}_PASS',
            'RPMI_LINUX_WAKEUP_PASS')

    def _validate_reset_poweroff(self):
        exec_command(self, 'echo RPMI_LINUX_RESET_POWEROFF_BEGIN; poweroff -f')
        wait_for_console_pattern(self, 'RPMI_LINUX_RESET_POWEROFF_BEGIN')
        wait_for_console_pattern(self, 'reboot: Power down')
        self.vm.wait()

    def _validate_reset_reboot(self):
        exec_command(self, 'echo RPMI_LINUX_RESET_REBOOT_BEGIN; reboot -f')
        wait_for_console_pattern(self, 'RPMI_LINUX_RESET_REBOOT_BEGIN')
        wait_for_console_pattern(self, 'reboot: Restarting system')
        wait_for_console_pattern(self, 'OpenSBI')

    def test_linux_guest_service_enumeration(self):
        self._boot_guest(smp=4)
        self._expect_rpmi_firmware_and_linux()
        self._validate_hsm_boot_start('0-3')

    def test_linux_guest_hsm_stop(self):
        self._boot_guest(smp=2)
        self._expect_rpmi_firmware_and_linux()
        self._validate_hsm_boot_start('0-1')
        self._validate_hsm_stop()

    def test_linux_guest_suspend(self):
        self._boot_guest(smp=1)
        self._expect_rpmi_firmware_and_linux()
        self._validate_system_suspend()

    def test_linux_guest_reset_poweroff(self):
        self._boot_guest(smp=1)
        self._expect_rpmi_firmware_and_linux()
        self._validate_reset_poweroff()

    def test_linux_guest_reset_reboot(self):
        self._boot_guest(smp=1)
        self._expect_rpmi_firmware_and_linux()
        self._validate_reset_reboot()


if __name__ == '__main__':
    QemuSystemTest.main()
