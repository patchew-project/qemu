/* SPDX-License-Identifier: GPL-2.0-or-later */

	.option	norvc
	.option	norelax

	.text
	.global _start
_start:
	/* Program counters 3 and 4 while no event is selected. */
	csrw	0x323, zero		/* mhpmevent3 */
	li	t0, -256
	csrw	0xb03, t0		/* mhpmcounter3 */
	csrw	0x324, zero		/* mhpmevent4 */
	li	t0, -512
	csrw	0xb04, t0		/* mhpmcounter4 */

	/* Count the same event in both counters with overflow enabled. */
	li	t0, 2
	csrw	0x323, t0
	csrw	0x324, t0

	/* Cross the 64-bit unsigned overflow boundary. */
	.rept	1024
	nop
	.endr

	/* OF must be sticky and LCOFIP must pend even with LCOFIE clear. */
	li	t4, 0
	csrr	t0, 0x323
	srli	t1, t0, 63
	xori	t1, t1, 1
	or	t4, t4, t1
	csrr	t0, 0x324
	srli	t1, t0, 63
	xori	t1, t1, 1
	or	t4, t4, t1

	csrr	t0, mip
	li	t1, 1 << 13
	and	t0, t0, t1
	sltu	t0, zero, t0
	xori	t0, t0, 1
	or	t4, t4, t0

	/* Both counters wrap and continue counting after overflow. */
	csrr	t0, 0xb03
	li	t1, -256
	sltu	t0, t0, t1
	xori	t0, t0, 1
	or	t4, t4, t0
	csrr	t0, 0xb04
	li	t1, -512
	sltu	t0, t0, t1
	xori	t0, t0, 1
	or	t4, t4, t0

	/* After selecting write misses, read misses must not increment HPM3. */
	csrw	0x323, zero		/* mhpmevent3 */
	csrw	0xb03, zero		/* mhpmcounter3 */
	li	t0, 0x10019		/* DTLB read miss */
	csrw	0x323, t0
	li	t0, 0x1001b		/* DTLB write miss */
	csrw	0x323, t0
	sfence.vma
	lla	t2, stale_probe
	lw	t3, 0(t2)
	csrr	t0, 0xb03
	or	t4, t4, t0

	/* Both counters must count a DTLB read miss. */
	csrw	0x323, zero		/* mhpmevent3 */
	csrw	0x324, zero		/* mhpmevent4 */
	csrw	0xb03, zero		/* mhpmcounter3 */
	csrw	0xb04, zero		/* mhpmcounter4 */
	li	t0, 0x10019		/* DTLB read miss */
	csrw	0x323, t0
	csrw	0x324, t0
	sfence.vma
	lla	t2, tlb_probe
	lw	t3, 0(t2)
	csrr	t0, 0xb03
	csrr	t1, 0xb04
	sltu	t2, zero, t0
	xori	t2, t2, 1
	or	t4, t4, t2
	sltu	t2, zero, t1
	xori	t2, t2, 1
	or	t4, t4, t2
	xor	t0, t0, t1
	sltu	t0, zero, t0
	or	t4, t4, t0

	/* Disabling HPM3 must leave HPM4 counting the same event. */
	csrr	t5, 0xb03
	csrr	t6, 0xb04
	csrw	0x323, zero		/* mhpmevent3 */
	sfence.vma
	lla	t2, tlb_probe2
	lw	t3, 0(t2)
	csrr	t0, 0xb03
	xor	t0, t0, t5
	or	t4, t4, t0
	csrr	t0, 0xb04
	sltu	t0, t6, t0
	xori	t0, t0, 1
	or	t4, t4, t0

	lla	a1, semiargs
	li	t0, 0x20026		/* ADP_Stopped_ApplicationExit */
	sd	t0, 0(a1)
	sd	t4, 8(a1)
	li	a0, 0x20		/* TARGET_SYS_EXIT_EXTENDED */

	/* Semihosting call sequence. */
	.balign	16
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.

	.data
	/* Give each DTLB probe a separate page. */
	.balign	4096
stale_probe:
	.word	0
	.balign	4096
tlb_probe:
	.word	0
	.balign	4096
tlb_probe2:
	.word	0
	.balign	16
semiargs:
	.space	16
