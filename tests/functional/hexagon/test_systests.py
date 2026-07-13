#!/usr/bin/env python3
#
# Copyright(c) Qualcomm Innovation Center, Inc. All Rights Reserved.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import re
import time
import unittest

from qemu_test import QemuSystemTest, Asset, wait_for_console_pattern


_TARBALL_BIN_PATH = os.path.join(
    "systests_standalone_package",
    "StandaloneSysTests_6.4.0.2_v68",
    "bin",
)


class SysTestsStandaloneTests(QemuSystemTest):
    SYSTEST_TIMEOUT_SEC = 30

    ASSET_TARBALL = Asset(
        "https://github.com/qualcomm/qemu-hexagon-testing/releases/download/v0.2.9/systests_standalone.tar.gz",
        "db961ea3fcc389b478b3d5c2f3bac60bec87e7f3d7efef5e58a9ae8ee78d0e40",
    )

    def setUp(self):
        super().setUp()
        self.archive_extract(self.ASSET_TARBALL)
        self.bin_dir = os.path.join(self.workdir, _TARBALL_BIN_PATH)
        self._orig_cwd = os.getcwd()
        os.chdir(self.workdir)
        self.addCleanup(os.chdir, self._orig_cwd)

    def tearDown(self):
        if hasattr(self, "_orig_cwd"):
            os.chdir(self._orig_cwd)
        super().tearDown()

    def binary(self, name):
        """Return the full path to binary *name* inside bin_dir."""
        return os.path.join(self.bin_dir, name)

    def run_exit_zero(self, binary_name, *extra_args, machine="V66G_1024"):
        """Launch *binary_name* and assert it exits with code 0.

        :param binary_name: name of the binary inside bin_dir.
        :param extra_args: optional pairs of (flag, value) strings passed
            to set_vm_arg(), e.g. ('-append', 'myarg').
        :param machine: QEMU machine type (default "V66G_1024").
        """
        self.set_machine(machine)
        self.set_vm_arg("-display", "none")
        self.set_vm_arg("-kernel", self.binary(binary_name))
        for flag, value in zip(extra_args[::2], extra_args[1::2]):
            self.set_vm_arg(flag, value)
        self.vm.launch()
        self.vm.wait(timeout=60.0)
        self.assertEqual(self.vm.exitcode(), 0,
                         f"Test {binary_name} exited with "
                         f"code {self.vm.exitcode()}, expected 0")

    def run_console_pattern(self, binary_name, pattern, *extra_args,
                            machine="V66G_1024"):
        """Launch *binary_name* and wait for *pattern* on the semihosting console.

        :param binary_name: name of the binary inside bin_dir.
        :param pattern: string pattern to wait for via wait_for_console_pattern.
        :param extra_args: optional pairs of (flag, value) strings passed
            to set_vm_arg(), e.g. ('-append', 'myarg').
        :param machine: QEMU machine type (default "V66G_1024").
        """
        self.set_machine(machine)
        self.set_vm_arg("-display", "none")
        self.set_vm_arg("-kernel", self.binary(binary_name))
        for flag, value in zip(extra_args[::2], extra_args[1::2]):
            self.set_vm_arg(flag, value)
        self.vm.set_console(semihosting=True)
        self.vm.launch()
        try:
            wait_for_console_pattern(self, pattern)
        finally:
            self.vm.kill()

    def test_fopen(self):
        """fopen reads a file passed via --append and verifies its contents."""
        with open(os.path.join(self.workdir, "dummy.so"), "w") as f:
            f.write("valid\n")
        self.run_exit_zero("fopen", "-append", "dummy.so")

    def test_ftrunc(self):
        """ftrunc truncates _testfile_ftrunc from 6 bytes to 1 byte."""
        ftrunc_path = os.path.join(self.workdir, "_testfile_ftrunc")
        with open(ftrunc_path, "w") as f:
            f.write("valid\n")
        # Sleep 1 s so mtime change is observable
        time.sleep(1)
        self.run_exit_zero("ftrunc")
        self.assertEqual(
            os.path.getsize(ftrunc_path),
            1,
            "_testfile_ftrunc should be 1 byte after ftrunc",
        )

    def test_dirent(self):
        """dirent lists a directory passed via --append; output must be
        '. .. fileA fileB'."""
        dirent_dir = os.path.join(self.workdir, "_dirent_testdir")
        os.makedirs(dirent_dir, exist_ok=True)
        open(os.path.join(dirent_dir, "fileA"), "w").close()
        open(os.path.join(dirent_dir, "fileB"), "w").close()
        self.run_console_pattern(
            "dirent", ". .. fileA fileB", "-append", "_dirent_testdir"
        )

    def test_access(self):
        """access checks R_OK|W_OK on _testfile_access."""
        with open(os.path.join(self.workdir, "_testfile_access"), "w") as f:
            f.write("valid\n")
        self.run_exit_zero("access")

    def test_semihost(self):
        semihost_dir = os.path.join(self.workdir, "_semihost_dir")
        os.makedirs(semihost_dir, exist_ok=True)
        open(os.path.join(semihost_dir, "fileA"), "w").close()
        open(os.path.join(semihost_dir, "fileB"), "w").close()
        self.run_console_pattern("semihost", "PASS", "-append", "arg1", "arg2")

if __name__ == "__main__":
    QemuSystemTest.main()
