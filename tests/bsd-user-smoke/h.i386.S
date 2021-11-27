# Copyright (c) 2021 Warner Losh
# SPDX-License-Identifier: BSD-2-Clause

#include <sys/syscall.h>
#define STDOUT_FILENO	1

	.text
	.file	"hello.s"
	.globl	qemu_start                            # -- Begin function qemu_start
	.p2align	4, 0x90
	.type	qemu_start,@function
qemu_start:                                   # @qemu_start
# %bb.0:                                # %entry
	pushl	%ebp
	movl	%esp, %ebp

	# write(1, .L.str, sizeof(.L.str) - 1)
	pushl	$(.L.strEnd - .L.str - 1)
	pushl	$.L.str
	pushl	$STDOUT_FILENO
	pushl	%eax			# dummy return address
	movl	$SYS_write, %eax
	int	$0x80
	addl	$16, %esp		# 3 args + 1 dummy

	# _exit(0)
	pushl	$0
	pushl	%eax			# dummy return address
	movl	$SYS_exit, %eax
	int	$0x80
.Lfunc_end0:
	.size	qemu_start, .Lfunc_end0-qemu_start
                                        # -- End function
	.type	.L.str,@object                  # @.str
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str:
	.asciz	"Hello World\n"
.L.strEnd:
	.size .L.str, .L.strEnd - .L.str
