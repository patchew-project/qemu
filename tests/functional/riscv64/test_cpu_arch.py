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
        """Run qemu-riscv64 with specified CPU option"""
        cmd = [self.qemu_bin, '-cpu', cpu_opt, bin_path]
        return run(cmd, text=True, capture_output=True)

    def test_arch_dump(self):
        """Test arch=dump prints ISA configuration and exits"""
        res = self.run_qemu('rv64,arch=dump')

        self.assertEqual(res.returncode, 0,
                         f"arch=dump should exit with 0, got {res.returncode}")

        # Check for expected output sections
        self.assertIn('RISC-V ISA Configuration', res.stdout)
        self.assertIn('Full ISA string:', res.stdout)
        self.assertIn('Standard Extensions (single-letter):', res.stdout)
        self.assertIn('Standard Extensions (multi-letter):', res.stdout)
        self.assertIn('Vendor Extensions:', res.stdout)

    def test_arch_dump_shows_enabled_extensions(self):
        """Test arch=dump correctly shows enabled extensions"""
        res = self.run_qemu('rv64,arch=dump')

        # Default rv64 should have these enabled
        self.assertRegex(res.stdout, r'i\s+enabled')
        self.assertRegex(res.stdout, r'm\s+enabled')
        self.assertRegex(res.stdout, r'a\s+enabled')
        self.assertRegex(res.stdout, r'f\s+enabled')
        self.assertRegex(res.stdout, r'd\s+enabled')
        self.assertRegex(res.stdout, r'c\s+enabled')

    def test_arch_dump_with_vector(self):
        """Test arch=dump shows vector extension when enabled"""
        res = self.run_qemu('rv64,v=true,arch=dump')

        self.assertEqual(res.returncode, 0)
        self.assertRegex(res.stdout, r'v\s+enabled')

    def test_arch_dump_position_independence(self):
        """Test arch=dump shows final config regardless of position"""
        # arch=dump before v=true
        res1 = self.run_qemu('rv64,arch=dump,v=true')
        # arch=dump after v=true
        res2 = self.run_qemu('rv64,v=true,arch=dump')

        self.assertEqual(res1.returncode, 0)
        self.assertEqual(res2.returncode, 0)

        # Both should show v enabled
        self.assertRegex(res1.stdout, r'v\s+enabled')
        self.assertRegex(res2.stdout, r'v\s+enabled')

    def test_arch_help(self):
        """Test arch=help prints list of supported extensions and exits"""
        res = self.run_qemu('rv64,arch=help')

        self.assertEqual(res.returncode, 0,
                         f"arch=help should exit with 0, got {res.returncode}")

        # Check for expected output sections
        self.assertIn('Supported RISC-V ISA Extensions', res.stdout)
        self.assertIn('Standard Extensions (single-letter):', res.stdout)
        self.assertIn('Standard Extensions (multi-letter):', res.stdout)
        self.assertIn('Vendor Extensions:', res.stdout)

    def test_arch_help_shows_extensions(self):
        """Test arch=help lists common extensions"""
        res = self.run_qemu('rv64,arch=help')

        # Check single-letter extensions with descriptions
        self.assertIn('Base integer instruction set', res.stdout)
        self.assertIn('Vector operations', res.stdout)

        # Check multi-letter extensions are listed
        self.assertIn('zba', res.stdout)
        self.assertIn('zbb', res.stdout)

    def test_arch_invalid_option(self):
        """Test invalid arch= option shows error with supported options"""
        res = self.run_qemu('rv64,arch=invalid')

        self.assertNotEqual(res.returncode, 0,
                            "Invalid arch option should fail")
        self.assertIn("unknown arch option 'invalid'", res.stderr)
        self.assertIn("Supported options:", res.stderr)


if __name__ == '__main__':
    QemuUserTest.main()
