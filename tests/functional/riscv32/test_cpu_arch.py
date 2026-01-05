#!/usr/bin/env python3
#
# Test RISC-V CPU arch= property
#
# Copyright (c) 2026 SiFive, Inc.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from subprocess import run

from qemu_test import QemuUserTest


class RiscvCpuArch(QemuUserTest):
    """Test RISC-V CPU arch= property"""

    def run_qemu(self, cpu_opt, bin_path='/bin/true'):
        """Run qemu-riscv32 with specified CPU option"""
        cmd = [self.qemu_bin, '-cpu', cpu_opt, bin_path]
        return run(cmd, text=True, capture_output=True)

    def test_arch_dump(self):
        """Test arch=dump prints ISA configuration and exits"""
        res = self.run_qemu('rv32,arch=dump')

        self.assertEqual(res.returncode, 0,
                         f"arch=dump should exit with 0, got {res.returncode}")

        # Check for expected output sections
        self.assertIn('RISC-V ISA Configuration', res.stdout)
        self.assertIn('Full ISA string:', res.stdout)
        self.assertIn('Standard Extensions (single-letter):', res.stdout)
        self.assertIn('Standard Extensions (multi-letter):', res.stdout)
        self.assertIn('Vendor Extensions:', res.stdout)
        # Check it's RV32
        self.assertIn('Base: RV32', res.stdout)

    def test_arch_dump_shows_enabled_extensions(self):
        """Test arch=dump correctly shows enabled extensions"""
        res = self.run_qemu('rv32,arch=dump')

        # Default rv32 should have these enabled
        self.assertRegex(res.stdout, r'i\s+enabled')
        self.assertRegex(res.stdout, r'm\s+enabled')
        self.assertRegex(res.stdout, r'a\s+enabled')
        self.assertRegex(res.stdout, r'f\s+enabled')
        self.assertRegex(res.stdout, r'd\s+enabled')
        self.assertRegex(res.stdout, r'c\s+enabled')

    def test_arch_help(self):
        """Test arch=help prints list of supported extensions and exits"""
        res = self.run_qemu('rv32,arch=help')

        self.assertEqual(res.returncode, 0,
                         f"arch=help should exit with 0, got {res.returncode}")

        # Check for expected output sections
        self.assertIn('Supported RISC-V ISA Extensions', res.stdout)
        self.assertIn('Standard Extensions (single-letter):', res.stdout)
        self.assertIn('Standard Extensions (multi-letter):', res.stdout)

    def test_arch_invalid_option(self):
        """Test invalid arch= option shows error with supported options"""
        res = self.run_qemu('rv32,arch=invalid')

        self.assertNotEqual(res.returncode, 0,
                            "Invalid arch option should fail")
        self.assertIn("unknown arch option 'invalid'", res.stderr)
        self.assertIn("Supported options:", res.stderr)


if __name__ == '__main__':
    QemuUserTest.main()
