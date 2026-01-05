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

    def test_arch_isa_string_basic(self):
        """Test arch=ISA-STRING enables specified extensions"""
        res = self.run_qemu('rv64,arch=rv64gc_zba_zbb,arch=dump')

        self.assertEqual(res.returncode, 0)

        # Check single-letter extensions from 'gc' (g = imafd_zicsr_zifencei)
        self.assertRegex(res.stdout, r'g\s+enabled')
        self.assertRegex(res.stdout, r'c\s+enabled')

        # Check multi-letter extensions
        self.assertRegex(res.stdout, r'zba\s+enabled')
        self.assertRegex(res.stdout, r'zbb\s+enabled')

    def test_arch_isa_string_g_expands(self):
        """Test arch=rv64g enables IMAFD + Zicsr + Zifencei"""
        res = self.run_qemu('rv64,arch=rv64g,arch=dump')

        self.assertEqual(res.returncode, 0)

        # G expands to IMAFD_Zicsr_Zifencei
        self.assertRegex(res.stdout, r'i\s+enabled')
        self.assertRegex(res.stdout, r'm\s+enabled')
        self.assertRegex(res.stdout, r'a\s+enabled')
        self.assertRegex(res.stdout, r'f\s+enabled')
        self.assertRegex(res.stdout, r'd\s+enabled')
        self.assertRegex(res.stdout, r'zicsr\s+enabled')
        self.assertRegex(res.stdout, r'zifencei\s+enabled')

    def test_arch_isa_string_b_expands(self):
        """Test arch=rv64ib enables Zba + Zbb + Zbs"""
        res = self.run_qemu('rv64,arch=rv64ib,arch=dump')

        self.assertEqual(res.returncode, 0)

        # B expands to Zba_Zbb_Zbs
        self.assertRegex(res.stdout, r'b\s+enabled')

    def test_arch_isa_string_flexible_order(self):
        """Test extensions can be in any order"""
        # Both should produce equivalent results
        res1 = self.run_qemu('rv64,arch=rv64gc_zba_zbb,arch=dump')
        res2 = self.run_qemu('rv64,arch=rv64gc_zbb_zba,arch=dump')

        self.assertEqual(res1.returncode, 0)
        self.assertEqual(res2.returncode, 0)
        self.assertRegex(res1.stdout, r'zba\s+enabled')
        self.assertRegex(res1.stdout, r'zbb\s+enabled')
        self.assertRegex(res2.stdout, r'zba\s+enabled')
        self.assertRegex(res2.stdout, r'zbb\s+enabled')

    def test_arch_isa_string_mixed_single_multi(self):
        """Test single-letter extensions can appear after underscores"""
        # rv64im_zba_afc should be equivalent to rv64imafc_zba
        res = self.run_qemu('rv64,arch=rv64im_zba_afc,arch=dump')

        self.assertEqual(res.returncode, 0)
        self.assertRegex(res.stdout, r'\bi\s+enabled')
        self.assertRegex(res.stdout, r'm\s+enabled')
        self.assertRegex(res.stdout, r'a\s+enabled')
        self.assertRegex(res.stdout, r'f\s+enabled')
        self.assertRegex(res.stdout, r'c\s+enabled')
        self.assertRegex(res.stdout, r'zba\s+enabled')

    def test_arch_isa_string_no_underscore_before_multi(self):
        """Test multi-letter extension can follow single-letter directly"""
        # rv64imazba should be equivalent to rv64ima_zba
        res1 = self.run_qemu('rv64,arch=rv64imazba,arch=dump')
        res2 = self.run_qemu('rv64,arch=rv64ima_zba,arch=dump')

        self.assertEqual(res1.returncode, 0)
        self.assertEqual(res2.returncode, 0)

        # Both should have ima and zba enabled
        for res in [res1, res2]:
            self.assertRegex(res.stdout, r'\bi\s+enabled')
            self.assertRegex(res.stdout, r'm\s+enabled')
            self.assertRegex(res.stdout, r'a\s+enabled')
            self.assertRegex(res.stdout, r'zba\s+enabled')

    def test_arch_isa_string_repeated_extension(self):
        """Test extensions can be repeated"""
        res = self.run_qemu('rv64,arch=rv64gc_zba_zba,arch=dump')

        self.assertEqual(res.returncode, 0)
        self.assertRegex(res.stdout, r'zba\s+enabled')

    def test_arch_isa_string_resets_extensions(self):
        """Test arch= resets all extensions first"""
        # zba=true before arch= should be reset
        res = self.run_qemu('rv64,zba=true,arch=rv64gc,arch=dump')

        self.assertEqual(res.returncode, 0)
        self.assertRegex(res.stdout, r'zba\s+disabled')

    def test_arch_combined_with_property(self):
        """Test arch= can be combined with individual properties"""
        # arch=rv64gc,zba=true should enable zba
        res = self.run_qemu('rv64,arch=rv64gc,zba=true,arch=dump')

        self.assertEqual(res.returncode, 0)
        self.assertRegex(res.stdout, r'zba\s+enabled')

    def test_arch_isa_string_xlen_mismatch(self):
        """Test arch=ISA-STRING rejects XLEN mismatch"""
        res = self.run_qemu('rv64,arch=rv32i')

        self.assertNotEqual(res.returncode, 0)
        self.assertIn("RV32 but CPU is RV64", res.stderr)

    def test_arch_isa_string_unknown_extension(self):
        """Test arch=ISA-STRING rejects unknown extension"""
        # Unknown extension after valid single-letters (ix -> i + x)
        res = self.run_qemu('rv64,arch=rv64ix')

        self.assertNotEqual(res.returncode, 0)
        self.assertIn("unknown extension 'x'", res.stderr)

        # Unknown multi-letter extension after underscore
        res = self.run_qemu('rv64,arch=rv64i_xyzfoo')

        self.assertNotEqual(res.returncode, 0)
        self.assertIn("unknown extension 'xyzfoo'", res.stderr)

    def test_arch_isa_string_first_ext_validation(self):
        """Test arch=ISA-STRING requires first extension to be i, e, or g"""
        # First extension must be i, e, or g
        res = self.run_qemu('rv64,arch=rv64m')

        self.assertNotEqual(res.returncode, 0)
        self.assertIn("first extension after rv64 must be 'i', 'e', or 'g'",
                      res.stderr)

    def test_arch_isa_string_base_ext_only_first(self):
        """Test i, e, g can only appear as the first extension"""
        # 'e' cannot appear after other extensions
        res = self.run_qemu('rv64,arch=rv64imae')

        self.assertNotEqual(res.returncode, 0)
        self.assertIn("'e' must be the first extension", res.stderr)

        # 'g' cannot appear after 'i'
        res = self.run_qemu('rv64,arch=rv64ig')

        self.assertNotEqual(res.returncode, 0)
        self.assertIn("'g' must be the first extension", res.stderr)

        # 'i' cannot appear after underscore
        res = self.run_qemu('rv64,arch=rv64g_i')

        self.assertNotEqual(res.returncode, 0)
        self.assertIn("'i' must be the first extension", res.stderr)

    def test_arch_isa_string_underscore_separated_single(self):
        """Test single-letter extensions can be separated by underscores"""
        # rv64i_m_a_f_d_c should be equivalent to rv64imafdc
        res = self.run_qemu('rv64,arch=rv64i_m_a_f_d_c,arch=dump')

        self.assertEqual(res.returncode, 0)
        self.assertRegex(res.stdout, r'\bi\s+enabled')
        self.assertRegex(res.stdout, r'm\s+enabled')
        self.assertRegex(res.stdout, r'a\s+enabled')
        self.assertRegex(res.stdout, r'f\s+enabled')
        self.assertRegex(res.stdout, r'd\s+enabled')
        self.assertRegex(res.stdout, r'c\s+enabled')

    def test_arch_isa_string_zvl_requires_vector(self):
        """Test zvl*b requires v or zve* extension"""
        res = self.run_qemu('rv64,arch=rv64g_zvl128b')

        self.assertNotEqual(res.returncode, 0)
        self.assertIn("zvl*b requires v or zve* extension", res.stderr)

    def test_arch_profile_rva23u64(self):
        """Test arch=rva23u64 enables RVA23 profile extensions"""
        res = self.run_qemu('rv64,arch=rva23u64,arch=dump')

        self.assertEqual(res.returncode, 0)

        # RVA23U64 mandates vector extension
        self.assertRegex(res.stdout, r'\bv\s+enabled')

        # RVA23U64 mandates these extensions
        self.assertRegex(res.stdout, r'zicond\s+enabled')
        self.assertRegex(res.stdout, r'zimop\s+enabled')
        self.assertRegex(res.stdout, r'zcmop\s+enabled')
        self.assertRegex(res.stdout, r'zcb\s+enabled')
        self.assertRegex(res.stdout, r'zfa\s+enabled')
        self.assertRegex(res.stdout, r'zvbb\s+enabled')

    def test_arch_profile_rva22u64(self):
        """Test arch=rva22u64 enables RVA22 profile extensions"""
        res = self.run_qemu('rv64,arch=rva22u64,arch=dump')

        self.assertEqual(res.returncode, 0)

        # RVA22U64 mandates these MISA extensions
        self.assertRegex(res.stdout, r'\bi\s+enabled')
        self.assertRegex(res.stdout, r'm\s+enabled')
        self.assertRegex(res.stdout, r'a\s+enabled')
        self.assertRegex(res.stdout, r'f\s+enabled')
        self.assertRegex(res.stdout, r'd\s+enabled')
        self.assertRegex(res.stdout, r'c\s+enabled')

        # RVA22U64 mandates zicsr and zifencei
        self.assertRegex(res.stdout, r'zicsr\s+enabled')
        self.assertRegex(res.stdout, r'zifencei\s+enabled')

    def test_arch_profile_case_insensitive(self):
        """Test arch=PROFILE is case-insensitive"""
        res = self.run_qemu('rv64,arch=RVA23U64,arch=dump')

        self.assertEqual(res.returncode, 0)
        # Should enable vector like lowercase version
        self.assertRegex(res.stdout, r'\bv\s+enabled')

    def test_arch_help_shows_profiles(self):
        """Test arch=help lists available profiles"""
        res = self.run_qemu('rv64,arch=help')

        self.assertEqual(res.returncode, 0)
        self.assertIn('Profiles', res.stdout)
        self.assertIn('rva22u64', res.stdout)
        self.assertIn('rva22s64', res.stdout)
        self.assertIn('rva23u64', res.stdout)
        self.assertIn('rva23s64', res.stdout)

    def test_arch_profile_with_extensions(self):
        """Test arch=PROFILE_EXT enables profile plus additional extensions"""
        res = self.run_qemu('rv64,arch=rva23u64_zbkb_zkne,arch=dump')

        self.assertEqual(res.returncode, 0)
        # Profile extensions should be enabled
        self.assertRegex(res.stdout, r'\bv\s+enabled')
        # Additional extensions should also be enabled
        self.assertRegex(res.stdout, r'zbkb\s+enabled')
        self.assertRegex(res.stdout, r'zkne\s+enabled')

    def test_arch_profile_with_unknown_extension(self):
        """Test arch=PROFILE_EXT rejects unknown extensions"""
        res = self.run_qemu('rv64,arch=rva23u64_unknown')

        self.assertNotEqual(res.returncode, 0)
        self.assertIn("unknown extension 'unknown'", res.stderr)

    def test_arch_help_shows_zvl(self):
        """Test arch=help lists zvl*b extensions"""
        res = self.run_qemu('rv64,arch=help')

        self.assertEqual(res.returncode, 0)
        self.assertIn('Vector Length Extensions', res.stdout)
        self.assertIn('zvl32b', res.stdout)
        self.assertIn('zvl128b', res.stdout)

    def test_arch_isa_string_zvl(self):
        """Test arch=ISA-STRING accepts zvl*b extensions"""
        res = self.run_qemu('rv64,arch=rv64gcv_zvl256b,arch=dump')

        self.assertEqual(res.returncode, 0)
        self.assertIn('VLEN=256', res.stdout)
        self.assertIn('zvl256b', res.stdout)
        # Check zvl*b is included in Full ISA string
        self.assertRegex(res.stdout, r'Full ISA string:.*_zvl256b')

    def test_arch_dump_shows_vlen(self):
        """Test arch=dump shows vector length configuration"""
        res = self.run_qemu('rv64,arch=rv64gcv_zvl512b,arch=dump')

        self.assertEqual(res.returncode, 0)
        self.assertIn('Vector length:', res.stdout)
        self.assertIn('VLEN=512', res.stdout)

    def test_arch_isa_string_zvl_takes_max(self):
        """Test multiple zvl*b extensions take maximum value"""
        # zvl128b followed by zvl512b - should use 512
        res1 = self.run_qemu('rv64,arch=rv64gcv_zvl128b_zvl512b,arch=dump')
        self.assertEqual(res1.returncode, 0)
        self.assertIn('VLEN=512', res1.stdout)

        # zvl512b followed by zvl128b - should still use 512
        res2 = self.run_qemu('rv64,arch=rv64gcv_zvl512b_zvl128b,arch=dump')
        self.assertEqual(res2.returncode, 0)
        self.assertIn('VLEN=512', res2.stdout)

        # Three zvl extensions - should use maximum (1024)
        res3 = self.run_qemu('rv64,arch=rv64gcv_zvl256b_zvl1024b_zvl512b,arch=dump')
        self.assertEqual(res3.returncode, 0)
        self.assertIn('VLEN=1024', res3.stdout)

    def test_arch_isa_string_implied_vlen(self):
        """Test extensions imply minimum VLEN correctly"""
        # zve64f implies zvl64b, so zvl32b should be ignored
        res1 = self.run_qemu('rv64,arch=rv64i_zve64f_zvl32b,arch=dump')
        self.assertEqual(res1.returncode, 0)
        self.assertIn('VLEN=64', res1.stdout)

        # v implies zvl128b, so zvl64b should be ignored
        res2 = self.run_qemu('rv64,arch=rv64gcv_zvl64b,arch=dump')
        self.assertEqual(res2.returncode, 0)
        self.assertIn('VLEN=128', res2.stdout)

        # zve64x alone should have VLEN=64
        res3 = self.run_qemu('rv64,arch=rv64i_zve64x,arch=dump')
        self.assertEqual(res3.returncode, 0)
        self.assertIn('VLEN=64', res3.stdout)


if __name__ == '__main__':
    QemuUserTest.main()
