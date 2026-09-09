#!/usr/bin/env python3
#
# Functional test that boots an OpenBMC image on the Axiado SCM3003 EVK
#
# Author: Kuan-Jui Chiu <kchiu@axiado.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset, wait_for_console_pattern


class AxiadoScm3003Machine(LinuxKernelTest):

    ASSET_SDK_IMAGES = Asset(
        ('https://github.com/axiado/openbmc/releases/download/v0.1.0/'
         'evk-axiado-qemu.tar.gz'),
        'a163b78ed07a43e08323395e2df63c213fbc98b8bc412083a11e94564e26548b')

    def setUp(self):
        super().setUp()

        SDK_IMAGE_DIR = 'evk-axiado-qemu'

        self.archive_extract(self.ASSET_SDK_IMAGES)
        self.kernel_path = self.scratch_file(SDK_IMAGE_DIR, 'Image')
        self.dtb_path = self.scratch_file(SDK_IMAGE_DIR,
                                          'ax3000-scm3003-qemu.dtb')
        self.initrd_path = self.scratch_file(
            SDK_IMAGE_DIR, 'obmc-phosphor-initramfs-evk-axiado-qemu.cpio.xz')
        self.disk_path = self.scratch_file(
            SDK_IMAGE_DIR, 'obmc-phosphor-image-evk-axiado-qemu.wic.qcow2')

    def test_aarch64_axiado_scm3003_boot(self):
        self.require_accelerator('tcg')
        self.set_machine('axiado-scm3003')

        # The SoC has four Cadence UARTs; the console is the fourth one.
        self.vm.set_console(console_index=3)
        self.vm.add_args(
            '-kernel', self.kernel_path,
            '-dtb', self.dtb_path,
            '-initrd', self.initrd_path,
            '-drive', 'file=%s,if=sd,format=qcow2,snapshot=on' % self.disk_path,
            '-append', 'console=ttyPS3,115200 earlycon nr_cpus=1 '
                       'rootwait root=PARTLABEL=rofs-a')

        self.vm.launch()

        self.wait_for_console_pattern('Linux version')
        self.wait_for_console_pattern('Machine model: Axiado AX3000-SCM3003')

        self.wait_for_console_pattern('Run /init as init process')

        # The initramfs prints this after probing the SD card for the rwfs.
        self.wait_for_console_pattern('blkdev: /dev/mmcblk0 sector_size: 512')

        # Reaching systemd proves the initramfs mounted the squashfs out of
        # the phram window and switch_root'ed into it.
        self.wait_for_console_pattern('Hostname set to <evk-axiado-qemu>')

if __name__ == '__main__':
    LinuxKernelTest.main()
