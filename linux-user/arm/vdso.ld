/*
 * Linker script for linux arm replacement vdso.
 *
 * Copyright 2021 Linaro, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

VERSION {
        LINUX_2.6 {
        global:
                __vdso_clock_gettime;
                __vdso_gettimeofday;
                __vdso_clock_getres;
                __vdso_clock_gettime64;

        local: *;
        };
}


PHDRS {
        phdr            PT_PHDR         FLAGS(4) PHDRS;
        data            PT_LOAD         FLAGS(6) FILEHDR PHDRS;
        text            PT_LOAD         FLAGS(5);
        dynamic         PT_DYNAMIC      FLAGS(4);
        eh_frame_hdr    PT_GNU_EH_FRAME;
        note            PT_NOTE         FLAGS(4);
}

SECTIONS {
        /*
         * We can't prelink to any address without knowing something about
         * the virtual memory space of the host, since that leaks over into
         * the available memory space of the guest.
         */
        . = SIZEOF_HEADERS;

        /*
         * The following, including the FILEHDRS and PHDRS, are modified
         * when we relocate the binary.  We want them to be initially
         * writable for the relocation; we'll force them read-only after.
         */
        .note           : { *(.note*) }         :data :note
        .dynamic        : { *(.dynamic) }       :data :dynamic
        .dynsym         : { *(.dynsym) }        :data
        /*
         * There ought not be any real read-write data.
         * But since we manipulated the segment layout,
         * we have to put these sections somewhere.
         */
        .data           : {
                *(.data*)
                *(.sdata*)
                *(.got.plt) *(.got)
                *(.gnu.linkonce.d.*)
                *(.bss*)
                *(.dynbss*)
                *(.gnu.linkonce.b.*)
        }

        .rodata         : { *(.rodata*) }
        .hash           : { *(.hash) }
        .gnu.hash       : { *(.gnu.hash) }
        .dynstr         : { *(.dynstr) }
        .gnu.version    : { *(.gnu.version) }
        .gnu.version_d  : { *(.gnu.version_d) }
        .gnu.version_r  : { *(.gnu.version_r) }
        .eh_frame_hdr   : { *(.eh_frame_hdr) }  :data :eh_frame_hdr
        .eh_frame       : { *(.eh_frame) }      :data

        . = ALIGN(4096);
        .text           : { *(.text*) }         :text
}
