#!/usr/bin/env python3
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from pathlib import Path

from qemu_test import Asset, QemuSystemTest, wait_for_console_pattern


class PacketTests(QemuSystemTest):
    timeout = 180

    ASSET_TARBALL = Asset(
        "https://github.com/qualcomm/qemu-hexagon-testing/releases/"
        "download/v0.3.5/systests_standalone.tar.gz",
        "640b0aa60af7d3e561c25c33079c31dd6a7077fed757f9d29c0ad9ee9f2eca19",
    )

    def test_packets(self):
        self.archive_extract(self.ASSET_TARBALL)
        kernel = Path(self.scratch_file(
            "systests_standalone_package",
            "StandaloneSysTests_6.7.0.0_v68", "bin", "packet_tests"))
        self.assertTrue(kernel.is_file(), str(kernel))
        self.set_machine("V85NA_1")
        self.set_vm_arg("-display", "none")
        self.set_vm_arg("-kernel", str(kernel))
        self.vm.set_console(semihosting=True)
        self.vm.launch()
        names = (
            "hmx_address", "hlx_disable", "fetch_cause", "fetch_pc", "fault_slot",
            "fault_validity", "inactive_badva", "store_priority",
            "alignment_priority", "store_rollback", "vmemu_fault",
        )
        try:
            for name in names:
                wait_for_console_pattern(self, "PASS " + name, "FAIL")
            wait_for_console_pattern(self, "PASS packet_tests: 11 cases",
                                     "FAIL")
        finally:
            self.vm.kill()


if __name__ == "__main__":
    QemuSystemTest.main()
