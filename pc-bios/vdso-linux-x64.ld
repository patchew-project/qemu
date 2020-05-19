/*
 * Linker script for linux x64 replacement vdso.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

VERSION {
	LINUX_2.6 {
	global:
		clock_gettime;
		__vdso_clock_gettime;
		gettimeofday;
		__vdso_gettimeofday;
		getcpu;
		__vdso_getcpu;
		time;
		__vdso_time;
	local: *;
	};
}

PHDRS {
	phdr		PT_PHDR		FLAGS(4) PHDRS;
	data		PT_LOAD		FLAGS(6) FILEHDR PHDRS;
	text		PT_LOAD		FLAGS(5);
	dynamic		PT_DYNAMIC	FLAGS(4);
	note		PT_NOTE		FLAGS(4);
	/* ??? Various versions of ld don't know PT_GNU_EH_FRAME. */
	eh_frame_hdr	0x6474e550;
}

SECTIONS {
	/* ??? We can't really prelink to any address without knowing
	   something about the virtual memory space of the host, since
	   that leaks over into the available memory space of the guest.  */
	. = SIZEOF_HEADERS;

	/* The following, including the FILEHDRS and PHDRS, are modified
	   when we relocate the binary.  We want them to be initially
	   writable for the relocation; we'll force them read-only after.  */
	.dynamic	: { *(.dynamic) }	:data :dynamic
	.dynsym		: { *(.dynsym) }	:data
	.data		: {
		/* There ought not be any real read-write data.
		   But since we manipulated the segment layout,
		   we have to put these sections somewhere.  */
		*(.data*)
		*(.sdata*)
		*(.got.plt) *(.got)
		*(.gnu.linkonce.d.*)
		*(.bss*)
		*(.dynbss*)
		*(.gnu.linkonce.b.*)
	}

	. += 4096;
	.hash		: { *(.hash) }		:text
	.gnu.hash	: { *(.gnu.hash) }
	.dynstr		: { *(.dynstr) }
	.gnu.version	: { *(.gnu.version) }
	.gnu.version_d	: { *(.gnu.version_d) }
	.gnu.version_r	: { *(.gnu.version_r) }
	.note		: { *(.note*) }		:text :note
	.eh_frame_hdr	: { *(.eh_frame_hdr) }	:text :eh_frame_hdr
	.eh_frame	: { *(.eh_frame) }	:text
	.rodata		: { *(.rodata) }

	. = ALIGN(0x100);
	.text		: { *(.text*) }		:text	=0x90909090
}
