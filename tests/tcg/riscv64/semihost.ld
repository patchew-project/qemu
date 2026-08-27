/*
 * Linker script for the RISC-V system tests
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

ENTRY(_start)

MEMORY
{
    /*
     * Virt machine, RAM starts at 2GB
     *
     * Separate sections for executable and writable data to avoid linker
     * warnings.
     */
    TXT (rx) : ORIGIN = 0x80000000, LENGTH = 2M
    DAT (rw) : ORIGIN = 0x80000000 + 2M, LENGTH = 2M
}

SECTIONS
{
    .text : {
        *(.text._start)
        *(.text)
    } > TXT

    .rodata : {
        *(.rodata .rodata.*)
        *(.eh_frame)
    } > TXT

    .got : {
        *(.got .got.plt)
    } > DAT

    .data : {
        *(.data .data.* .sdata .sdata.*)
    } > DAT

    .bss : {
        *(.bss .bss.* .sbss .sbss.*)
    } > DAT

    /* Stack grows down from the end of the data region */
    __stack_top = ORIGIN(DAT) + LENGTH(DAT);
}
