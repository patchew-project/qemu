	.option norvc

	.text
	.global _start
_start:
	# Set up trap vector
	lla	t0, trap
	csrw	mtvec, t0

	# Set up timer
	lui	t1, 0x02004
	sd	zero, 0(t1) # MTIMECMP0

	# Enable timer interrupts
	li	t0, 0x80
	csrrs	zero, mie, t0
	csrrsi	zero, mstatus, 0x8

	# Find out when to stop
	call	rtc_get
	li	t0, 60
	slli	t0, t0, 30 # Approx. 10e9 ns
	add	t0, t0, a0

	# Loop with memory accesses
	la	t1, semiargs
0:
	ld	t2, 0(t1)
	sd	t2, 0(t1)
	call	rtc_get
	bltu	a0, t0, 0b

	li	a0, 0
	lla	a1, semiargs
	li	t0, 0x20026	# ADP_Stopped_ApplicationExit
	sd	t0, 0(a1)
	sd	a0, 8(a1)
	li	a0, 0x20	# TARGET_SYS_EXIT_EXTENDED

	# Semihosting call sequence
	.balign	16
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.

rtc_get:
	# Get current time from the goldfish RTC
	lui	t3, 0x0101
	lw	a0, 0(t3)
	lw	t3, 4(t3)
	slli	t3, t3, 32
	add	a0, a0, t3
	ret

trap:
	lui	t5, 0x0200c
	ld	t6, -0x8(t5) # MTIME
	addi	t6, t6, 100
	lui	t5, 0x02004
	sd	t6, 0(t5) # MTIMECMP
	mret

	.data
	.balign	16
semiargs:
	.space	16
