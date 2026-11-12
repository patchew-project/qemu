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

class TuxRunAarch64Test(TuxRunBaselineTest):

    ASSET_ARM64_KERNEL = Asset(
        'https://storage.tuxboot.com/kernels/6.18.54/arm64/Image.gz',
        'e297f12682af7e88735a0aae38e6fbd137192b516ab5f0fc1a81c4bcc3992e80')
    ASSET_ARM64_VMLINUX = Asset(
        'https://storage.tuxboot.com/kernels/6.18.54/arm64/vmlinux.xz',
        'aa9b984337e975528162ed873cf18a072b27be8e4f48e6cf0723e9b490c63171')
    ASSET_ARM64_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20260924/arm64/rootfs.ext4.zst',
        '4ab8cec9d1e17dbb1abdf2ae1939ad4f9d0dc2466230f6a119a7c1845cdef0b1')

    def test_arm64(self):
        self.set_machine('virt')
        self.require_accelerator('tcg')
        self.cpu='cortex-a57'
        self.console='ttyAMA0'
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_ARM64_KERNEL,
                           rootfs_asset=self.ASSET_ARM64_ROOTFS)

    def test_arm64_kvm(self):
        self.set_machine('virt')
        self.require_accelerator('kvm')
        self.cpu='host'
        self.console='ttyAMA0'
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_ARM64_KERNEL,
                           rootfs_asset=self.ASSET_ARM64_ROOTFS,
                           accel="kvm")

    @skipIfMissingEnv("QEMU_TEST_GDB")
    def test_arm64_kvm_gdb(self):
        self.set_machine('virt')
        self.require_accelerator('kvm')
        self.cpu='host'
        self.console='ttyAMA0'
        self.wait_for_shutdown=False
        self.common_tuxrun_gdb(kernel_asset=self.ASSET_ARM64_KERNEL,
                               vmlinux_asset=self.ASSET_ARM64_VMLINUX,
                               rootfs_asset=self.ASSET_ARM64_ROOTFS,
                               accel="kvm")

    ASSET_ARM64BE_KERNEL = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/arm64be/Image',
        'fd6af4f16689d17a2c24fe0053cc212edcdf77abdcaf301800b8d38fa9f6e109')
    ASSET_ARM64BE_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/arm64be/rootfs.ext4.zst',
        'f5e9371b62701aab8dead52592ca7488c8a9e255c9be8d7635c7f30f477c2c21')

    def test_arm64be(self):
        self.set_machine('virt')
        self.require_accelerator('tcg')
        self.cpu='cortex-a57'
        self.console='ttyAMA0'
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_ARM64BE_KERNEL,
                           rootfs_asset=self.ASSET_ARM64BE_ROOTFS)

if __name__ == '__main__':
    TuxRunBaselineTest.main()
