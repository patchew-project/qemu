# Copyright (c) 2021 Warner Losh
# SPDX-License-Identifier: BSD-2-Clause

#include <sys/syscall.h>
#define STDOUT_FILENO	1

	.text
	.globl	qemu_start
	.p2align	4, 0x90
qemu_start:

	# write(1, .L.str, sizeof(.L.str) - 1)
	movq	$.L.str, %rsi
	movl	$STDOUT_FILENO, %edi
	movl	$len, %edx
	movl	$SYS_write, %eax
	syscall

	# _exit(0)
	xorl	%edi, %edi
	movl	$SYS_exit, %eax
	syscall

	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str:
	.asciz	"Hello World\n"
.L.strEnd:
len=.L.strEnd - .L.str - 1
