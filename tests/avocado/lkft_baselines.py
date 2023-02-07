# Functional test that boots known good lkft images
#
# Copyright (c) 2023 Linaro Ltd.
#
# Author:
#  Alex Bennée <alex.bennee@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import time

from avocado_qemu import QemuSystemTest
from avocado_qemu import exec_command, exec_command_and_wait_for_pattern
from avocado_qemu import wait_for_console_pattern
from avocado.utils import process
from avocado.utils.path import find_command


class LKFTBaselineTest(QemuSystemTest):
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 root=/dev/vda'

    def setUp(self):
        super().setUp()

        # We need zstd for all the lkft tests
        zstd = find_command('zstd', False)
        if zstd is False:
            self.cancel('Could not find "zstd", which is required to '
                        'decompress rootfs')
        self.zstd = zstd

        # Process the LKFT specific tags, most machines work with
        # reasonable defaults but we sometimes need to tweak the
        # config.

        # The lfkt tag matches the root directory
        self.lkft = self.params.get('lkft',
                                    default = self._get_unique_tag_val('lkft'))

        # Most Linux's use ttyS0 for their serial port
        console = self.params.get('console',
                                  default = self._get_unique_tag_val('console'))
        if console:
            self.console = console
        else:
            self.console = "ttyS0"

        # Does the machine shutdown QEMU nicely on "halt"
        self.shutdown = self.params.get('shutdown',
                                    default = self._get_unique_tag_val('shutdown'))

        # The name of the kernel Image file
        image = self.params.get('image',
                                default = self._get_unique_tag_val('image'))
        if not image:
            self.image = "Image"
        else:
            self.image = image

        # The block device drive type
        drive = self.params.get('drive',
                                default = self._get_unique_tag_val('drive'))
        if not drive:
            self.drive = "virtio-blk-device"
        else:
            self.drive = drive

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing',
                                 vm=vm)

    def fetch_lkft_assets(self, dt=None):
        """
        Fetch the LKFT assets. They are stored in a standard way so we
        use the per-test tags to fetch details.
        """
        base_url = f"https://storage.tuxboot.com/{self.lkft}/"
        kernel_image =  self.fetch_asset(base_url + self.image)
        disk_image_zst = self.fetch_asset(base_url + "rootfs.ext4.zst")

        cmd = f"{self.zstd} -d {disk_image_zst} -o {self.workdir}/rootfs.ext4"
        process.run(cmd)

        if dt:
            dtb = self.fetch_asset(base_url + dt)
        else:
            dtb = None

        return (kernel_image, self.workdir + "/rootfs.ext4", dtb)

    def prepare_run(self, kernel, disk, dtb=None):
        """
        Setup to run and add the common parameters to the system
        """
        self.vm.set_console()

        # all block devices are raw ext4's
        blockdev = "driver=raw,file.driver=file," \
            + f"file.filename={disk},node-name=hd0"

        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + \
            f" console={self.console}"

        self.vm.add_args('-kernel', kernel,
                         '-append', kernel_command_line,
                         '-blockdev', blockdev,
                         '-device', f"{self.drive},drive=hd0")

        # Some machines need an explicit DTB
        if dtb:
            self.vm.add_args('-dtb', dtb)

    def run_tuxtest_tests(self, haltmsg):
        """
        Wait for the system to boot up, wait for the login prompt and
        then do a few things on the console. Trigger a shutdown and
        wait to exit cleanly.
        """
        self.wait_for_console_pattern("Welcome to TuxTest")
        time.sleep(0.1)
        exec_command(self, 'root')
        time.sleep(0.1)
        exec_command(self, 'cat /proc/interrupts')
        time.sleep(0.1)
        exec_command(self, 'cat /proc/self/maps')
        time.sleep(0.1)
        exec_command(self, 'uname -a')
        time.sleep(0.1)
        exec_command_and_wait_for_pattern(self, 'halt', haltmsg)

        # Wait for VM to shut down gracefully if it can
        if self.shutdown == "nowait":
            self.vm.shutdown()
        else:
            self.vm.wait()

    def common_lkft(self, dt=None, haltmsg="reboot: System halted"):
        """
        Common path for LKFT tests. Unless we need to do something
        special with the command line we can process most things using
        the tag metadata.
        """
        (kernel, disk, dtb) = self.fetch_lkft_assets(dt)

        self.prepare_run(kernel, disk, dtb)
        self.vm.launch()
        self.run_tuxtest_tests(haltmsg)

    def test_arm64(self):
        """
        :avocado: tags=arch:aarch64
        :avocado: tags=cpu:cortex-a57
        :avocado: tags=machine:virt
        :avocado: tags=lkft:arm64
        :avocado: tags=console:ttyAMA0
        :avocado: tags=shutdown:nowait
        """
        self.common_lkft()

    def test_arm64be(self):
        """
        :avocado: tags=arch:aarch64
        :avocado: tags=cpu:cortex-a57
        :avocado: tags=machine:virt
        :avocado: tags=lkft:arm64be
        :avocado: tags=console:ttyAMA0
        :avocado: tags=shutdown:nowait
        """
        self.common_lkft()

    def test_armv5(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=cpu:arm926
        :avocado: tags=machine:versatilepb
        :avocado: tags=lkft:armv5
        :avocado: tags=image:zImage
        :avocado: tags=drive:virtio-blk-pci
        :avocado: tags=console:ttyAMA0
        :avocado: tags=shutdown:nowait
        """
        self.common_lkft(dt="versatile-pb.dtb")

    def test_armv7(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=cpu:cortex-a15
        :avocado: tags=machine:virt
        :avocado: tags=lkft:armv7
        :avocado: tags=image:zImage
        :avocado: tags=console:ttyAMA0
        :avocado: tags=shutdown:nowait
        """
        self.common_lkft()

    def test_armv7be(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=cpu:cortex-a15
        :avocado: tags=machine:virt
        :avocado: tags=lkft:armv7be
        :avocado: tags=image:zImage
        :avocado: tags=console:ttyAMA0
        :avocado: tags=shutdown:nowait
        """
        self.common_lkft()

    def test_i386(self):
        """
        :avocado: tags=arch:i386
        :avocado: tags=cpu:coreduo
        :avocado: tags=machine:q35
        :avocado: tags=lkft:i386
        :avocado: tags=image:bzImage
        :avocado: tags=drive:virtio-blk-pci
        :avocado: tags=shutdown:nowait
        """
        self.common_lkft()

    # def test_mips32(self):
    #     """
    #     :avocado: tags=arch:mips
    #     :avocado: tags=machine:malta
    #     :avocado: tags=lkft:mips32
    #     :avocado: tags=image:vmlinux
    #     :avocado: tags=drive:virtio-blk-pci
    #     :avocado: tags=shutdown:nowait
    #     """
    #     self.common_lkft()

    def test_riscv32(self):
        """
        :avocado: tags=arch:riscv32
        :avocado: tags=machine:virt
        :avocado: tags=lkft:riscv32
        """
        self.common_lkft()

    def test_riscv64(self):
        """
        :avocado: tags=arch:riscv64
        :avocado: tags=machine:virt
        :avocado: tags=lkft:riscv64
        """
        self.common_lkft()

    def test_s390(self):
        """
        :avocado: tags=arch:s390x
        :avocado: tags=lkft:s390
        :avocado: tags=image:bzImage
        :avocado: tags=drive:virtio-blk-ccw
        :avocado: tags=shutdown:nowait
        """
        self.common_lkft(haltmsg="Requesting system halt")
