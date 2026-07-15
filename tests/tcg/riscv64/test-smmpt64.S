/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Test for the Smmpt64 (Supervisor Memory Protection Table) extension,
 * per SMMTT specification v0.4.9.
 *
 * Smmpt64 uses a 5-level MPT with 8-byte entries.  Unlike the other RV64
 * modes its root table is 32 KiB (2^12 entries, pn[4] = SPA[63:52], 12 bits)
 * and must be aligned to a 32 KiB boundary; the non-root levels are the usual
 * 4 KiB / 9-bit tables (16 pages per leaf, pi = SPA[15:12]).  See
 * smmpt-common.S for the MPRV trick, the shared address layout and the
 * exit-code convention.
 *
 * For the low (< 4 GiB) addresses exercised here pn[4], pn[3] and pn[2] are
 * 0, so the upper levels are single-entry chains:
 *
 *   L4 (root) @ 0x8040_0000 : entry[0]  -> non-leaf, PPN(L3)=0x80410
 *                             (32 KiB / 32 KiB-aligned)
 *   L3        @ 0x8041_0000 : entry[0]  -> non-leaf, PPN(L2)=0x80420
 *   L2        @ 0x8042_0000 : entry[0]  -> non-leaf, PPN(L1)=0x80430
 *   L1        @ 0x8043_0000 : entry[64] -> non-leaf, PPN(L0)=0x80440
 *   L0        @ 0x8044_0000 :
 *     entry[80]  -> non-NAPOT leaf (LEAF_ALL_XWR)  covering 0x8050_0000+
 *     entry[81]  -> V=0 (invalid)                  covering 0x8051_0000
 *     entry[82]  -> leaf with a reserved bit set    covering 0x8052_0000
 *     entry[96]  -> NAPOT leaf, G=4, XWR=RW         covering 0x8060_0000
 *     entry[128] -> NAPOT leaf, reserved G=5        covering 0x8080_0000
 *
 *   mmpt = MODE(3=Smmpt64)<<60 | PPN(0x80400)   (PPN[2:0] must be 0)
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
	/* L4[0] = non-leaf -> L3 (PPN 0x80410); root is 32 KiB aligned */
	li	t0, 0x80400000
	li	t1, 0x20104001
	sd	t1, 0(t0)
	/* L3[0] = non-leaf -> L2 (PPN 0x80420) */
	li	t0, 0x80410000
	li	t1, 0x20108001
	sd	t1, 0(t0)
	/* L2[0] = non-leaf -> L1 (PPN 0x80430) */
	li	t0, 0x80420000
	li	t1, 0x2010C001
	sd	t1, 0(t0)
	/* L1[64] = non-leaf -> L0 (PPN 0x80440) */
	li	t0, 0x80430000
	li	t1, 0x20110001
	sd	t1, 0x200(t0)		/* 64 * 8 = 0x200 */

	li	t0, 0x80440000
	/* L0[80] = non-NAPOT leaf carrying all XWR encodings (pi0..pi7) */
	li	t1, LEAF_ALL_XWR
	sd	t1, 0x280(t0)		/* 80 * 8 = 0x280 */
	/* L0[81] = 0: invalid entry (V=0) */
	sd	x0, 0x288(t0)		/* 81 * 8 = 0x288 */
	/* L0[82] = leaf with reserved bit 3 set */
	li	t1, 0x30B
	sd	t1, 0x290(t0)		/* 82 * 8 = 0x290 */
	/* L0[96] = NAPOT leaf: V|L|N | XWR=RW | G=4 */
	li	t1, 0x4307
	sd	t1, 0x300(t0)		/* 96 * 8 = 0x300 */
	/* L0[128] = NAPOT leaf with reserved G=5 */
	li	t1, 0x5307
	sd	t1, 0x400(t0)		/* 128 * 8 = 0x400 */

	/* Program mmpt: MODE=3 (Smmpt64), PPN = 0x80400 */
	li	t0, 0x3000000000080400
	csrw	0x382, t0		/* CSR_MMPT */

	/* mfence.pa must not fault in M-mode */
	TEST_MFENCE_PA 1

	/* Run the shared permission and structural checks */
	RUN_LEAF_CHECKS

	/* All tests passed */
	li	a0, 0
	j	_exit

	SMMPT_HARNESS
