#!/usr/bin/env python3
#
# Functional tests for GPU blob support. This is a directed test to
# exercise the blob creation and removal features of virtio-gpu. You
# can find the source code for microkernel test here:
#   https://gitlab.com/epilys/qemu-880-repro
#
# Copyright (c) 2025 Linaro Ltd.
#
# Authors:
#  Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
#  Alex Bennée <alex.bennee@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu.machine.machine import VMLaunchFailure

from qemu_test import Asset
from qemu_test import wait_for_console_pattern
from qemu_test.linuxkernel import LinuxKernelTest

class Aarch64VirtBlobTest(LinuxKernelTest):

    ASSET_BLOB = Asset('https://fileserver.linaro.org/s/kE4nCFLdQcoBF9t/'
                       'download?path=%2Fblob-test&files=qemu-880.bin',
                       '2f6ab85d0b156c94fcedd2c4c821c5cbd52925a2de107f8e2d569ea2e34e42eb')

    def test_virtio_gpu_blob(self):

        self.set_machine('virt')
        self.require_accelerator("tcg")

        blob = self.ASSET_BLOB.fetch()

        self.vm.set_console()

        self.vm.add_args("-machine", "virt,memory-backend=mem0,accel=tcg",
                         '-m', '4G',
                         '-cpu', 'max',
                         '-kernel', blob,
                         '-object', 'memory-backend-memfd,share=on,id=mem0,size=4G',
                         '-global', 'virtio-mmio.force-legacy=false',
                         '-nic', 'none',
                         '-device',
                         'virtio-gpu-gl,hostmem=128M,blob=true,venus=true',
                         '-display', 'egl-headless,gl=on',
                         '-d', 'guest_errors')

        try:
            self.vm.launch()
        except VMLaunchFailure as excp:
            if "old virglrenderer, blob resources unsupported" in excp.output:
                self.skipTest("No blob support for virtio-gpu")
            elif "old virglrenderer, venus unsupported" in excp.output:
                self.skipTest("No venus support for virtio-gpu")
            elif "egl: no drm render node available" in excp.output:
                self.skipTest("Can't access host DRM render node")
            elif "'type' does not accept value 'egl-headless'" in excp.output:
                self.skipTest("egl-headless support is not available")
            elif "'type' does not accept value 'dbus'" in excp.output:
                self.skipTest("dbus display support is not available")
            elif "eglInitialize failed: EGL_NOT_INITIALIZED" in excp.output:
                self.skipTest("EGL failed to initialize on this host")
            else:
                self.log.info("unhandled launch failure: %s", excp.output)
                raise excp

        self.wait_for_console_pattern('[INFO] virtio-gpu test finished')
        # the test should cleanly exit


if __name__ == '__main__':
    LinuxKernelTest.main()
