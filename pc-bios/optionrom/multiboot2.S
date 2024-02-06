/*
 * Multiboot2 Option ROM
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Original Multiboot Option ROM:
 * Copyright Novell Inc, 2009
 *   Authors: Alexander Graf <agraf@suse.de>
 *
 * Adaption to Multiboot2 specs:
 * Copyright Goswin von Brederlow, 2011
 *   Authors: Goswin von Brederlow <goswin-v-b@web.de>
 *
 * Cleaned up and merged by:
 * Copyright Jens Nyberg, 2024
 *   Authors: Jens Nyberg <jens.nyberg@gmail.com>
 *
 */

#include "optionrom.h"

#define BOOT_ROM_PRODUCT "multiboot2 loader"

#define MULTIBOOT_MAGIC		0x36d76289

#define GS_PROT_JUMP		0
#define GS_GDT_DESC		8

/* Memory layout (page tables only for 64 bit):
 * 0x2000 L4 page table
 * 0x3000 L3 page table
 * 0x4000 L2 page table
 * ...
 * 0x7000 L2 page table
 * 0x8000 bootinfo tags
 */

#define PGTABLE			0x2000

BOOT_ROM_START

run_multiboot:

	cli
	cld

	mov		%cs, %eax
	shl		$0x4, %eax
	mov		%eax, %ebp /* used below to jump to 64bit */

	/* set up a long jump descriptor that is PC relative */

	/* move stack memory to %gs */
	mov		%ss, %ecx
	shl		$0x4, %ecx
	mov		%esp, %ebx
	add		%ebx, %ecx
	sub		$0x20, %ecx
	sub		$0x30, %esp
	shr		$0x4, %ecx
	mov		%cx, %gs

	/* now push the indirect jump decriptor there */
	mov		(prot_jump), %ebx
	add		%eax, %ebx
	movl		%ebx, %gs:GS_PROT_JUMP
	mov		$8, %bx
	movw		%bx, %gs:GS_PROT_JUMP + 4

	/* fix the gdt descriptor to be PC relative */
	movw		(gdt_desc), %bx
	movw		%bx, %gs:GS_GDT_DESC
	movl		(gdt_desc+2), %ebx
	add		%eax, %ebx
	movl		%ebx, %gs:GS_GDT_DESC + 2

	xor		%eax, %eax
	mov		%eax, %es

	/* Read the bootinfo struct into RAM */
	read_fw_blob(FW_CFG_INITRD)

	/* FS = bootinfo_struct */
	read_fw		FW_CFG_INITRD_ADDR
	shr		$4, %eax
	mov		%ax, %fs
	mov		%ax, %es	/* for int 0x15 */

	/* %fs:%edi = bootinfo size = offset of mmap tag */
	movl 		%fs:0, %edi

#define MULTIBOOT_TAG_TYPE_MMAP              6
	/* Create mmap tag */
	movl		$6, %fs:0(%edi)		/* type mmap */
	movl		$24, %fs:8(%edi)	/* entry_size */
	movl		$0, %fs:12(%edi)	/* entry_version */

	/* Initialize multiboot mmap structs using int 0x15(e820) */
	xor		%ebx, %ebx
	/* mmap starts at byte 16 */
	addl		$16, %edi

mmap_loop:
	/* entry size (mmap struct) & max buffer size (int15) */
	movl		$20, %ecx
	/* e820 */
	movl		$0x0000e820, %eax
	/* 'SMAP' magic */
	movl		$0x534d4150, %edx
	int		$0x15

mmap_check_entry:
	/* last entry? then we're done */
	jb		mmap_done
	and		%bx, %bx
	jz		mmap_done
	/* valid entry, so let's loop on */

mmap_next_entry:
	/* Advance %edi by sizeof(struct multiboot_mmap_entry) = 24 */
	addl		$24, %edi
	jmp		mmap_loop

mmap_done:
	addl		$24, %edi		/* advance past entry */
	/* Create end tag */
	movl		$0, %fs:0(%edi)		/* type */
	movl		$8, %fs:4(%edi)		/* size */
	leal		8(%edi), %ebx		/* size of all tags */

	/* set mmap tag size */
	mov		%edi, %eax	/* save offset after mmap */
	mov 		%fs:0, %edi	/* offset of mmap tag */
	sub		%edi, %eax	/* size of mmap tag */
	movl		%eax, %fs:4(%edi) /* size */

	/* Store size of all tags */
	movl		%ebx, %fs:0

real_to_prot:
	/* Load the GDT before going into protected mode */
lgdt:
	data32 lgdt	%gs:GS_GDT_DESC

	/* get us to protected mode now */
	movl		$1, %eax
	movl		%eax, %cr0

	/* the LJMP sets CS for us and gets us to 32-bit */
ljmp32:
	data32 ljmp	*%gs:GS_PROT_JUMP

prot_mode:
.code32
	/* initialize all other segments */
	movl		$0x10, %eax
	movl		%eax, %ss
	movl		%eax, %ds
	movl		%eax, %es
	movl		%eax, %fs
	movl		%eax, %gs

	/* Read the kernel and modules into RAM */
	read_fw_blob(FW_CFG_KERNEL)

	/* 32bit or 64bit mode? */
	read_fw		FW_CFG_KERNEL_64BIT
	cmpl		$0, %eax
	jne		ljmp64

	/* Jump off to the kernel */
	read_fw		FW_CFG_KERNEL_ENTRY
	mov		%eax, %ecx

	/* EBX contains a pointer to the bootinfo struct */
	read_fw		FW_CFG_INITRD_ADDR
	movl		%eax, %ebx

	/* EAX has to contain the magic */
	movl		$MULTIBOOT_MAGIC, %eax
ljmp2:
	/* Jump to kernel in 32bit mode */
	jmp		*%ecx

/******************************************************************/
/* Set up 64bit mode                                              */
/******************************************************************/
ljmp64:
	/* Enable PAE */
	movl	%cr4, %eax
	btsl	$5, %eax
	movl	%eax, %cr4

 /*
  * Build early 4G boot pagetable
  */
	/* Initialize Page tables to 0 */
	leal	PGTABLE, %edi
	xorl	%eax, %eax
	movl	$((4096*6)/4), %ecx
	rep	stosl

	/* Build Level 4 */
	leal	PGTABLE + 0, %edi
	leal	0x1007 (%edi), %eax
	movl	%eax, 0(%edi)

	/* Build Level 3 */
	leal	PGTABLE + 0x1000, %edi
	leal	0x1007(%edi), %eax
	movl	$4, %ecx
1:	movl	%eax, 0x00(%edi)
	addl	$0x00001000, %eax
	addl	$8, %edi
	decl	%ecx
	jnz	1b

	/* Build Level 2 */
	leal	PGTABLE + 0x2000, %edi
	movl	$0x00000183, %eax
	movl	$2048, %ecx
1:	movl	%eax, 0(%edi)
	addl	$0x00200000, %eax
	addl	$8, %edi
	decl	%ecx
	jnz	1b

	/* Load Level 4 page table (page 128) */
	leal	PGTABLE, %eax
	movl	%eax, %cr3

	/* Enable long mode */
	movl	$0xc0000080, %ecx
	rdmsr
	btsl	$8, %eax
	wrmsr

	/* enable paging to activate long mode */
	movl	%cr0, %eax
	btsl	$1, %eax	/* protected mode */
	btsl	$31, %eax	/* paging */
	movl	%eax, %cr0

	/* Jump off to the kernel */
	read_fw		FW_CFG_KERNEL_ENTRY
	movl		%eax, %ebx

	/* pointer to the bootinfo struct */
	read_fw		FW_CFG_INITRD_ADDR
	movl		%eax, %ecx

	/* jump to 64bit mode */
	pushl	$0x28
	leal	startup_64(%ebp), %eax
	pushl	%eax
	lret

	.code64
startup_64:
_startup_64:
	/* EDI has to contain the magic, ESI the boot info */
	movl	%ebx, %eax	/* kernel entry address */
	movl	%ecx, %esi	/* pointer to boot info */
	movl	$MULTIBOOT_MAGIC, %edi
	jmp	*%rax

color:	/* collor me silly */
	addl	$0x1,0xb8000
	jmp	color

/******************************************************************/


/* Variables */
.align 4, 0
prot_jump:	.long prot_mode
		.short 8

.align 4, 0
gdt:
	/* 0x00: null segment */
.byte	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

	/* 0x08: code segment */
	/* base=0, limit=0xfffff, type=32bit code exec/read, DPL=0, 4k */
.byte	0xff, 0xff, 0x00, 0x00, 0x00, 0x9a, 0xcf, 0x00

	/* 0x10: data segment */
	/* base=0, limit=0xfffff, type=32bit data read/write, DPL=0, 4k */
.byte	0xff, 0xff, 0x00, 0x00, 0x00, 0x92, 0xcf, 0x00

	/* 0x18: code segment */
	/* base=0, limit=0x0ffff, type=16bit code exec/read/conf, DPL=0, 1b */
.byte	0xff, 0xff, 0x00, 0x00, 0x00, 0x9e, 0x00, 0x00

	/* 0x20: data segment */
	/* base=0, limit=0x0ffff, type=16bit data read/write, DPL=0, 1b */
.byte	0xff, 0xff, 0x00, 0x00, 0x00, 0x92, 0x00, 0x00

	/* 0x28: code segment */
	/* base=0, limit=0xfffff, type=64bit code exec/read, DPL=0, 4k */
.byte	0xff, 0xff, 0x00, 0x00, 0x00, 0x9a, 0xaf, 0x00

	/* 0x30: ts descriptor */
.byte	0x00, 0x00, 0x00, 0x00, 0x00, 0x89, 0x80, 0x00
	/* 0x38: ts continued */
.byte	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

gdt_desc:
.short	(8 * 8) - 1
.long	gdt

BOOT_ROM_END
