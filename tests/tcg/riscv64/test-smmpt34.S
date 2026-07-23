/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Test for the Smmpt34 (Supervisor Memory Protection Table) extension,
 * per SMMTT specification v0.4.9.  Smmpt34 is the RV32-only scheme.
 *
 * Although Smmpt34 requires a 32-bit CPU, the program is run on
 * qemu-system-riscv64 with a 32-bit CPU (-cpu rv32), which lives in the
 * same target as the RV64 Smmpt tests.  It is therefore kept here in
 * tests/tcg/riscv64 alongside the other Smmpt tests and shares the same
 * harness (smmpt-common.S).  Build it for RV32 and run it as:
 *
 *   $CC -march=rv32ima_zicsr -mabi=ilp32 -I tests/tcg/riscv64 \
 *       tests/tcg/riscv64/test-smmpt34.S -Wa,--noexecstack -c -o smmpt34.o
 *   $LD -m elf32lriscv -T tests/tcg/riscv64/semihost.ld smmpt34.o -o smmpt34
 *   qemu-system-riscv64 -cpu rv32,x-smmpt=true -M virt -display none \
 *       -semihosting -device loader,file=smmpt34
 *
 * Smmpt34 uses a 2-level MPT with 4-byte entries (8 pages per leaf,
 * pi = SPA[14:12]).  The supervisor physical address is partitioned as
 * pn[1] = SPA[33:25] (9-bit root index), pn[0] = SPA[24:15] (10-bit level-0
 * index) and range offset SPA[14:0].  For the RAM addresses used here
 * (0x8xxx_xxxx, < 4 GiB) pn[1] resolves to 64.  See smmpt-common.S for the
 * MPRV trick, the shared address layout and the exit-code convention.
 *
 * This test reuses the same shared harness and the same access addresses as
 * the RV64 tests; only the table geometry (4-byte entries, 2 levels, 10-bit
 * level-0 index) and the RV32 mmpt layout differ.
 *
 * MPT layout (Smmpt34, 2 levels, 4-byte entries):
 *
 *   L1 (root) @ 0x8040_0000 : entry[64]  -> non-leaf, PPN(L0)=0x80410
 *   L0        @ 0x8041_0000 :
 *     entry[160] -> non-NAPOT leaf (LEAF_ALL_XWR)  covering 0x8050_0000+
 *     entry[162] -> V=0 (invalid)                  covering 0x8051_0000
 *     entry[164] -> leaf with a reserved bit set    covering 0x8052_0000
 *     entry[192] -> NAPOT leaf, G=6, XWR=RW         covering 0x8060_0000
 *     entry[256] -> NAPOT leaf, reserved G=0        covering 0x8080_0000
 *
 *   mmpt = MODE(1=Smmpt34)<<30 | PPN(0x80400)
 */

#include "smmpt-common.S"

	.option	norvc

	.text
	.global	_start
_start:
	/* Install the M-mode trap handler */
	lla	t0, mtrap
	csrw	mtvec, t0
	csrw	medeleg, zero

	/* PMP entry 0: NAPOT covering the whole space, RWX. */
	li	t0, -1
	csrw	pmpaddr0, t0
	li	t0, 0x1f
	csrw	pmpcfg0, t0

	/* Build the MPT tables (M-mode stores bypass the MPT). */
	/* L1[64] = non-leaf -> L0 (PPN 0x80410): (0x80410 << 10) | V */
	li	t0, 0x80400000
	li	t1, 0x20104001
	sw	t1, 0x100(t0)		/* 64 * 4 = 0x100 */

	li	t0, 0x80410000
	/* L0[160] = non-NAPOT leaf carrying all XWR encodings (pi0..pi7) */
	li	t1, LEAF_ALL_XWR
	sw	t1, 0x280(t0)		/* 160 * 4 = 0x280 */
	/* L0[162] = 0: invalid entry (V=0) */
	sw	x0, 0x288(t0)		/* 162 * 4 = 0x288 */
	/* L0[164] = leaf with reserved bit 3 set (V|L|rsv|XWR[pi0]=RW) */
	li	t1, 0x30B
	sw	t1, 0x290(t0)		/* 164 * 4 = 0x290 */
	/* L0[192] = NAPOT leaf: V|L|N | XWR=RW(0x300) | G=6(0x6000) */
	li	t1, 0x6307
	sw	t1, 0x300(t0)		/* 192 * 4 = 0x300 */
	/* L0[256] = NAPOT leaf with reserved G=0: V|L|N | XWR=RW | G=0 */
	li	t1, 0x307
	sw	t1, 0x400(t0)		/* 256 * 4 = 0x400 */

	/* Program mmpt: MODE=1 (Smmpt34) at bits[31:30], PPN = 0x80400 */
	li	t0, 0x40080400
	csrw	0x382, t0		/* CSR_MMPT */

	/* mfence.pa must not fault in M-mode */
	TEST_MFENCE_PA 1

	/* Run the shared permission and structural checks */
	RUN_LEAF_CHECKS

	/* All tests passed */
	li	a0, 0
	j	_exit

	SMMPT_HARNESS
