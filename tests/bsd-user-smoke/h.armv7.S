# Copyright (c) 2021 Warner Losh
# SPDX-License-Identifier: BSD-2-Clause

#include <sys/syscall.h>
#define STDOUT_FILENO	1

	.text
	.file	"hello.s"
	.syntax unified
	.globl	qemu_start                            @ -- Begin function qemu_start
	.p2align	2
	.type	qemu_start,%function
	.code	32                              @ @qemu_start
qemu_start:
@ %bb.0:                                @ %entry

	# write(1, .L.str, sizeof(.L.str) - 1)
	movw	r1, :lower16:.L.str	@ Load hello world
	movt	r1, :upper16:.L.str
	ldr	r0, =STDOUT_FILENO
	ldr	r2, =(.L.strEnd - .L.str - 1)
	ldr	r7, =SYS_write
	swi	0

	# _exit(0)
	ldr	r0, =0			@ success
	ldr	r7, =SYS_exit
	swi	0
.Lfunc_end0:
	.size	qemu_start, .Lfunc_end0-qemu_start
                                        @ -- End function
	.type	.L.str,%object                  @ @.str
	.section	.rodata.str1.1,"aMS",%progbits,1
.L.str:
	.asciz	"Hello World\n"
.L.strEnd:
	.size .L.str, .L.strEnd - .L.str
