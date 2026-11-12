#!/usr/bin/env python3
#
# Functional test that boots known good tuxboot images the same way
# that tuxrun (www.tuxrun.org) does. This tool is used by things like
# the LKFT project to run regression tests on kernels.
#
# Copyright (c) 2023 Linaro Ltd.
#
# Author:
#  Alex Bennée <alex.bennee@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset, skipIfMissingEnv
from qemu_test.tuxruntest import TuxRunBaselineTest

class TuxRunS390xTest(TuxRunBaselineTest):

    ASSET_S390X_KERNEL = Asset(
        'https://storage.tuxboot.com/kernels/6.18.54/s390/bzImage',
        'b5022f97e243b8ecd51fc7ce4290a15806ecc51a41edbe777fc0fcdda567269e')
    ASSET_S390X_VMLINUX = Asset(
        'https://storage.tuxboot.com/kernels/6.18.54/s390/vmlinux.xz',
        'cd70050ddb638e81c2f1b96a4629c224aedd36bf8ae3b56b264b070e94642e25')
    ASSET_S390X_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20260924/s390/rootfs.ext4.zst',
        '5ba18c4d18831c50a0c743990f7bdee1d3b520629d423c55b668d8fbbac9c6f5')

    def test_s390(self):
        self.set_machine('s390-ccw-virtio')
        self.require_accelerator('tcg')
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_S390X_KERNEL,
                           rootfs_asset=self.ASSET_S390X_ROOTFS,
                           drive="virtio-blk-ccw",
                           haltmsg="Requesting system halt")

    def test_s390_kvm(self):
        self.set_machine('s390-ccw-virtio')
        self.require_accelerator('kvm')
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_S390X_KERNEL,
                           rootfs_asset=self.ASSET_S390X_ROOTFS,
                           drive="virtio-blk-ccw",
                           haltmsg="Requesting system halt",
                           accel="kvm")

    @skipIfMissingEnv("QEMU_TEST_GDB")
    def test_s390_kvm_gdb(self):
        self.set_machine('s390-ccw-virtio')
        self.require_accelerator('kvm')
        self.wait_for_shutdown=False
        self.common_tuxrun_gdb(kernel_asset=self.ASSET_S390X_KERNEL,
                               vmlinux_asset=self.ASSET_S390X_VMLINUX,
                               rootfs_asset=self.ASSET_S390X_ROOTFS,
                               symbol_off="0x3ffe0000000",
                               drive="virtio-blk-ccw",
                               accel="kvm")

if __name__ == '__main__':
    TuxRunBaselineTest.main()
